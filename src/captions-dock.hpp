#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>
#include <mutex>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QSlider;
class QCheckBox;
class QPlainTextEdit;
class QTimer;
class QProgressBar;

namespace tiktok {

class AudioCapture;
class WsClient;
class ApiClient;
class TrialTracker;
class OverlayInstaller;

// Vertical glassmorphic dock. We extend QFrame (NOT QDockWidget) so OBS's
// obs_frontend_add_dock_by_id wraps us in its own QDockWidget - that wrapper
// auto-persists geometry + visibility + floating state across OBS restarts.
// Previously we were a QDockWidget inside OBS's QDockWidget wrapper which
// double-nested + lost all the state on close.
class TikToolCaptionsDock : public QFrame {
    Q_OBJECT
public:
    static TikToolCaptionsDock *install();
    void shutdown();

private slots:
    void onAudioPickerChanged(int);
    void onCohostPickerChanged(int);
    void onLanguageChanged();
    void onStyleChanged();
    void onMasterToggle();
    void onInstallOverlay();
    void onLoginClicked();
    void onTopupClicked();
    void onWizardClicked();
    void onResetAllClicked();
    void onSignOutClicked();
    void onUiScaleSmaller();
    void onUiScaleLarger();
    void onAutoConfigureClicked();
    void onUsernameChanged();
    void onWatermarkToggled(bool);
    void onTrialChanged();
    void onWsConnectedChanged(bool);
    void onPartial(const QString &text, const QString &lang);
    void onFinal(const QString &text, const QString &lang,
                 const QString &translation, const QString &translationLang);
    void onMinutesUpdate(int minutesLeft);
    void onLevelMeter(double dbfs);

private:
    TikToolCaptionsDock();
    void buildUi();
    void applyStyle();
    void applyUiScale();
    void rebuildAudioSources();
    void pushSettingsToServer();
    void persistAndRestream();

    AudioCapture     *audio_       = nullptr;
    AudioCapture     *cohostAudio_ = nullptr;
    WsClient         *ws_          = nullptr;
    ApiClient        *api_         = nullptr;
    TrialTracker     *trial_       = nullptr;
    OverlayInstaller *installer_   = nullptr;
    // Latest co-host PCM frame (PCM16 LE mono 16 kHz, 1920 bytes) protected
    // by cohostMutex_. Main AudioCapture's frame callback reads it + mixes
    // into the outgoing WS frame on every push so we transcribe the streamer
    // + the opponent together in a single stream.
    QByteArray   cohostFrame_;
    qint64       cohostFrameTs_ = 0;
    std::mutex   cohostMutex_;

    // Status
    QLabel       *statusPill_  = nullptr;
    QLabel       *minutesLabel_= nullptr;
    QPushButton  *primaryCta_  = nullptr;
    QPushButton  *masterBtn_   = nullptr;
    QProgressBar *level_       = nullptr;

    // Identity (QLineEdit forward-declared above outside the namespace)
    ::QLineEdit  *usernameEdit_ = nullptr;
    QLabel       *usernameWarn_ = nullptr;

    // Audio
    QComboBox    *audioPicker_  = nullptr;
    QComboBox    *cohostPicker_ = nullptr;

    // Watermark toggle (forced ON for free trial accounts)
    QCheckBox    *watermarkToggle_ = nullptr;

    // Language
    QComboBox    *srcLang_     = nullptr;
    QComboBox    *toLang_      = nullptr;

    // Style
    QSlider      *fontSize_       = nullptr;
    QComboBox    *fontColor_      = nullptr;
    QSlider      *bgOpacity_      = nullptr;
    QSlider      *position_       = nullptr;
    QSlider      *maxLines_       = nullptr;
    QSlider      *xpad_           = nullptr;
    QCheckBox    *shadow_         = nullptr;
    QCheckBox    *uppercase_      = nullptr;
    QCheckBox    *showOriginal_   = nullptr;
    QCheckBox    *showLangLabels_ = nullptr;
    QCheckBox    *speakerNames_   = nullptr;
    QCheckBox    *speakerColors_  = nullptr;
    QComboBox    *align_          = nullptr;
    QComboBox    *font_           = nullptr;
    QComboBox    *bgMode_         = nullptr;

    // Preview
    QPlainTextEdit *transcript_ = nullptr;

    // Throttle saves
    QTimer       *saveDebounce_ = nullptr;

    bool streaming_ = false;
};

} // namespace tiktok
