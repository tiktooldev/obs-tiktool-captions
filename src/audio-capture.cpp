#include "audio-capture.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-source.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tiktool {

static bool enum_collect_cb(void *param, obs_source_t *src) {
    auto *list = static_cast<QStringList *>(param);
    const char *id = obs_source_get_id(src);
    if (!id) return true;
    // Pick anything that actually produces audio: input captures (wasapi, coreaudio,
    // pulse), media sources, and global audio sources. Skip video-only ids.
    uint32_t flags = obs_source_get_output_flags(src);
    if (flags & OBS_SOURCE_AUDIO) {
        const char *name = obs_source_get_name(src);
        if (name && *name) list->append(QString::fromUtf8(name));
    }
    return true;
}

QStringList AudioCapture::enumerateAudioSources() {
    QStringList out;
    obs_enum_sources(&enum_collect_cb, &out);
    // De-duplicate while keeping order.
    QStringList dedup;
    for (const auto &n : out) if (!dedup.contains(n)) dedup << n;
    return dedup;
}

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}
AudioCapture::~AudioCapture() { unbind(); }

void AudioCapture::setFrameCallback(FrameCb cb) {
    std::lock_guard<std::mutex> g(cbMutex_);
    cb_ = std::move(cb);
}

bool AudioCapture::bindToSource(const QString &displayName) {
    unbind();
    QByteArray nameUtf8 = displayName.toUtf8();
    obs_source_t *src = nullptr;
    if (!nameUtf8.isEmpty()) {
        src = obs_get_source_by_name(nameUtf8.constData());
    }
    if (!src) {
        // Fallback: walk sources and pick the first audio input capture.
        struct Picker { obs_source_t *picked = nullptr; };
        Picker p;
        obs_enum_sources([](void *param, obs_source_t *s) -> bool {
            auto *pp = static_cast<Picker *>(param);
            const char *id = obs_source_get_id(s);
            if (id && std::strstr(id, "audio_input_capture")) {
                pp->picked = obs_source_get_ref(s);
                return false;
            }
            return true;
        }, &p);
        src = p.picked;
    }
    if (!src) return false;

    source_    = src;
    boundName_ = QString::fromUtf8(obs_source_get_name(src));
    obs_source_add_audio_capture_callback(src, &AudioCapture::onAudioCapture, this);
    emit boundSourceChanged(boundName_);
    return true;
}

void AudioCapture::unbind() {
    if (source_) {
        obs_source_remove_audio_capture_callback(source_, &AudioCapture::onAudioCapture, this);
        obs_source_release(source_);
        source_ = nullptr;
    }
    boundName_.clear();
    resampleBuf_.clear();
    pendingPcm_.clear();
    srcSampleRate_ = 0;
    srcChannels_   = 0;
    resamplePos_   = 0.0;
}

void AudioCapture::onAudioCapture(void *param, obs_source_t * /*src*/,
                                  const struct audio_data *audio, bool muted) {
    auto *self = static_cast<AudioCapture *>(param);
    if (muted) return;
    if (!self->streaming_.load()) return;
    self->processAudio(audio);
}

void AudioCapture::processAudio(const struct audio_data *audio) {
    if (!audio || audio->frames == 0) return;

    // Discover the OBS-global audio format once. OBS hands us planar float;
    // mix down to mono float, then resample to 16 kHz, then quantize to PCM16.
    if (srcSampleRate_ == 0) {
        obs_audio_info info{};
        if (!obs_get_audio_info(&info)) return;
        srcSampleRate_ = (int)info.samples_per_sec;
        srcChannels_   = (int)get_audio_channels(info.speakers);
        if (srcChannels_ <= 0) srcChannels_ = 1;
    }

    const float *planes[8] = {nullptr};
    int channels = std::min(srcChannels_, 8);
    for (int i = 0; i < channels; ++i)
        planes[i] = reinterpret_cast<const float *>(audio->data[i]);

    const uint32_t frames = audio->frames;
    resampleBuf_.reserve(resampleBuf_.size() + frames);

    double maxAbs = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        float sum = 0.f;
        int valid = 0;
        for (int c = 0; c < channels; ++c) {
            if (!planes[c]) continue;
            sum += planes[c][i];
            valid++;
        }
        float mono = valid ? sum / float(valid) : 0.f;
        resampleBuf_.push_back(mono);
        maxAbs = std::max(maxAbs, (double)std::fabs(mono));
    }

    if (maxAbs > 0.0) {
        double dbfs = 20.0 * std::log10(std::min(1.0, maxAbs));
        emit levelMeter(dbfs);
    }

    // Resample using simple linear interpolation. For voice this is plenty;
    // higher-quality filters cost CPU and Soniox runs its own front-end anyway.
    const double ratio = double(srcSampleRate_) / double(OUT_SR);
    while (resamplePos_ + 1.0 < double(resampleBuf_.size())) {
        size_t i0 = (size_t)std::floor(resamplePos_);
        double frac = resamplePos_ - double(i0);
        float a = resampleBuf_[i0];
        float b = resampleBuf_[i0 + 1];
        float mixed = a + (b - a) * float(frac);
        // Clamp + quantize.
        if (mixed > 1.f) mixed = 1.f;
        if (mixed < -1.f) mixed = -1.f;
        pendingPcm_.push_back((int16_t)(mixed * 32767.f));
        resamplePos_ += ratio;
    }

    // Drop the consumed prefix; keep the tail for the next batch.
    size_t consumed = (size_t)std::floor(resamplePos_);
    if (consumed > 0 && consumed <= resampleBuf_.size()) {
        resampleBuf_.erase(resampleBuf_.begin(), resampleBuf_.begin() + consumed);
        resamplePos_ -= double(consumed);
    }
    // Hard cap on backlog to avoid runaway memory if the WS chokes.
    if (resampleBuf_.size() > (size_t)(srcSampleRate_ * 2)) {
        resampleBuf_.erase(resampleBuf_.begin(),
            resampleBuf_.begin() + (resampleBuf_.size() - srcSampleRate_));
        resamplePos_ = 0.0;
    }

    // Flush 60 ms frames.
    while (pendingPcm_.size() >= OUT_FRAME_SAMPLES) {
        FrameCb cb;
        {
            std::lock_guard<std::mutex> g(cbMutex_);
            cb = cb_;
        }
        if (cb) cb(reinterpret_cast<const uint8_t *>(pendingPcm_.data()),
                   OUT_FRAME_SAMPLES * sizeof(int16_t));
        pendingPcm_.erase(pendingPcm_.begin(), pendingPcm_.begin() + OUT_FRAME_SAMPLES);
    }
}

} // namespace tiktool
