#include "scene-setup.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <cstring>

namespace tiktok {

static const char *platformCameraSourceId() {
#if defined(_WIN32)
    return "dshow_input";
#elif defined(__APPLE__)
    return "av_capture_input";
#else
    return "v4l2_input";
#endif
}

bool SceneSetup::ensureVerticalCanvas() {
    // Read current OBS video info, only apply a reset if anything actually
    // differs - obs_reset_video is heavy (drops + rebuilds the output mix)
    // so we do not want to trigger it on every wizard re-run.
    obs_video_info ovi{};
    if (!obs_get_video_info(&ovi)) return false;

    const uint32_t targetBaseW   = 1080;
    const uint32_t targetBaseH   = 1920;
    const uint32_t targetOutputW = 1080;
    const uint32_t targetOutputH = 1920;
    const uint32_t targetFpsN    = 30;
    const uint32_t targetFpsD    = 1;

    if (ovi.base_width  == targetBaseW   && ovi.base_height  == targetBaseH
        && ovi.output_width  == targetOutputW && ovi.output_height == targetOutputH
        && ovi.fps_num == targetFpsN && ovi.fps_den == targetFpsD) {
        return true;
    }

    ovi.base_width    = targetBaseW;
    ovi.base_height   = targetBaseH;
    ovi.output_width  = targetOutputW;
    ovi.output_height = targetOutputH;
    ovi.fps_num       = targetFpsN;
    ovi.fps_den       = targetFpsD;
    if (ovi.output_format == VIDEO_FORMAT_NONE) ovi.output_format = VIDEO_FORMAT_NV12;

    // obs_reset_video must not run while an output is active; if anything is
    // capturing the encoder we abort + tell the streamer to stop recording /
    // streaming first.
    if (obs_video_active()) return false;

    return obs_reset_video(&ovi) == OBS_VIDEO_SUCCESS;
}

bool SceneSetup::ensureCameraInScene() {
    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return false;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    if (!scene) { obs_source_release(sceneSrc); return false; }

    // Walk the scene tree looking for an existing camera source. If one is
    // already present we do not add a second.
    struct Ctx { bool hasCamera = false; const char *id; };
    Ctx ctx{ false, platformCameraSourceId() };
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
        auto *c = static_cast<Ctx *>(param);
        obs_source_t *s = obs_sceneitem_get_source(item);
        if (!s) return true;
        const char *sid = obs_source_get_id(s);
        if (sid && std::strcmp(sid, c->id) == 0) {
            c->hasCamera = true;
            return false;
        }
        return true;
    }, &ctx);

    if (ctx.hasCamera) {
        obs_source_release(sceneSrc);
        return true;
    }

    // No camera in this scene. Probe the camera source type's properties to
    // pull the first device id from the system enumeration, create the
    // source with that default, add to the scene + pin it under the captions
    // overlay (lower z-order) so the captions ride on top.
    obs_data_t *settings = obs_data_create();
    obs_source_t *probe = obs_source_create_private(ctx.id, "tiktok-camera-probe", settings);
    QString pickedDeviceId, pickedDeviceName;
    if (probe) {
        obs_properties_t *props = obs_source_properties(probe);
        if (props) {
            obs_property_t *p = obs_properties_get(props,
#if defined(_WIN32)
                "video_device_id"
#elif defined(__APPLE__)
                "device"
#else
                "device_id"
#endif
            );
            if (p) {
                size_t n = obs_property_list_item_count(p);
                for (size_t i = 0; i < n; ++i) {
                    const char *name = obs_property_list_item_name(p, i);
                    const char *id   = obs_property_list_item_string(p, i);
                    if (id && *id && name && *name) {
                        pickedDeviceId   = QString::fromUtf8(id);
                        pickedDeviceName = QString::fromUtf8(name);
                        break;
                    }
                }
            }
            obs_properties_destroy(props);
        }
        obs_source_release(probe);
    }
    obs_data_release(settings);

    settings = obs_data_create();
    if (!pickedDeviceId.isEmpty()) {
        // The settings key name differs per platform; set them all - obs
        // ignores keys it does not recognize.
        QByteArray idUtf8 = pickedDeviceId.toUtf8();
        obs_data_set_string(settings, "video_device_id", idUtf8.constData());
        obs_data_set_string(settings, "device", idUtf8.constData());
        obs_data_set_string(settings, "device_id", idUtf8.constData());
    }
    obs_source_t *cam = obs_source_create(ctx.id, "TikTools Camera", settings, nullptr);
    obs_data_release(settings);
    if (!cam) { obs_source_release(sceneSrc); return false; }

    struct AddCtx { obs_source_t *src; obs_sceneitem_t *item; };
    AddCtx add{ cam, nullptr };
    obs_scene_atomic_update(scene, [](void *param, obs_scene_t *s) {
        auto *a = static_cast<AddCtx *>(param);
        a->item = obs_scene_add(s, a->src);
    }, &add);
    if (add.item) {
        // Vertical-fill the 1080x1920 canvas.
        vec2 pos{ 0.f, 0.f };
        obs_sceneitem_set_pos(add.item, &pos);
        vec2 bounds{ 1080.f, 1920.f };
        obs_sceneitem_set_bounds(add.item, &bounds);
        obs_sceneitem_set_bounds_type(add.item, OBS_BOUNDS_SCALE_OUTER);
        obs_sceneitem_set_bounds_alignment(add.item, OBS_ALIGN_CENTER);
        // Push the camera UNDER the captions overlay so captions always render
        // on top regardless of which got added first.
        obs_sceneitem_set_order(add.item, OBS_ORDER_MOVE_BOTTOM);
    }

    obs_source_release(cam);
    obs_source_release(sceneSrc);
    return true;
}

bool SceneSetup::ensureVirtualCameraOn() {
    if (obs_frontend_virtualcam_active()) return true;
    obs_frontend_start_virtualcam();
    return obs_frontend_virtualcam_active();
}

SceneSetup::Result SceneSetup::runAll() {
    Result r;
    r.canvasOk     = ensureVerticalCanvas();
    if (!r.canvasOk) r.error = "Could not set canvas to 1080x1920 - stop any active output first.";
    r.cameraOk     = ensureCameraInScene();
    r.virtualCamOk = ensureVirtualCameraOn();
    emit finished(r);
    return r;
}

} // namespace tiktok
