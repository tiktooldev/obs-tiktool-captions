#pragma once

#include <QObject>
#include <QString>

namespace tiktok {

// Idiot-proof OBS configuration for the "open OBS + open TikTok LIVE Studio
// + go live" flow. Each method below is idempotent: calling it twice on the
// same OBS instance is a no-op the second time.
class SceneSetup : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    struct Result {
        bool canvasOk        = false; // base/output set to 1080x1920 @ 30 fps
        bool cameraOk        = false; // dshow_input source present in current scene
        bool virtualCamOk    = false; // virtual camera output started
        QString cameraName;            // human-readable label of the picked camera
        QString error;                 // populated on partial failure
    };

    // One-shot configure-everything entry point. Drops the canvas to vertical,
    // adds a webcam source if the current scene has none, starts the OBS
    // virtual camera output. Returns whichever steps succeeded so the wizard
    // can render targeted error states.
    Result runAll();

    // Granular accessors for the dock to call individually.
    bool ensureVerticalCanvas();    // 1080x1920 base + output, 30 fps
    bool ensureCameraInScene();     // adds dshow_input / av_capture_input / v4l2_input
    bool ensureVirtualCameraOn();   // obs_frontend_start_virtualcam

signals:
    void finished(const Result &r);
};

} // namespace tiktok
