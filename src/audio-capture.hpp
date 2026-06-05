#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <cstdint>

#include <obs.h>

namespace tiktool {

// Listens to an OBS audio source the streamer selects (mic or aux). Resamples
// to mono PCM16 16 kHz which is what Soniox expects and the rest of the relay
// pipeline already serves. Emits chunked frames suitable for direct WS send.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    // Each frame is a contiguous PCM16 little-endian mono buffer. Callback fires
    // on a captured-audio thread; the receiver MUST be cheap or post to its own
    // thread. Default chunk = 60 ms = 1920 bytes at 16 kHz.
    using FrameCb = std::function<void(const uint8_t *data, size_t bytes)>;

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    // Enumerate currently-loaded OBS audio sources by name (display name).
    static QStringList enumerateAudioSources();

    void setFrameCallback(FrameCb cb);

    // Picks the OBS source by display name. If empty, falls back to the first
    // available audio_input_capture source. Returns whether bind succeeded.
    bool bindToSource(const QString &displayName);
    void unbind();

    // Toggles whether frames are emitted. Useful for pause/preview without
    // tearing down the OBS audio hook.
    void setStreaming(bool on) { streaming_.store(on); }
    bool isStreaming() const   { return streaming_.load(); }

    QString boundSourceName() const { return boundName_; }

signals:
    void boundSourceChanged(const QString &name);
    void levelMeter(double dbfs);

private:
    static void onAudioCapture(void *param, obs_source_t *src,
                               const struct audio_data *audio, bool muted);
    void processAudio(const struct audio_data *audio);

    obs_source_t *source_ = nullptr;
    QString       boundName_;
    FrameCb       cb_;
    std::mutex    cbMutex_;
    std::atomic<bool> streaming_{false};

    // Resampler state (linear interpolator, mono mixdown). Soniox accepts 16k
    // PCM16 LE. Lightweight; no swresample dependency.
    int           srcSampleRate_ = 0;
    int           srcChannels_   = 0;
    double        resamplePos_   = 0.0;
    std::vector<float> resampleBuf_;
    std::vector<int16_t> pendingPcm_;
    static constexpr int OUT_SR = 16000;
    static constexpr int FRAME_MS = 60;
    static constexpr int OUT_FRAME_SAMPLES = OUT_SR * FRAME_MS / 1000;
};

} // namespace tiktool
