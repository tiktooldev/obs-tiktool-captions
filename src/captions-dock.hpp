#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QString>

class QLabel;
class QPushButton;
class QComboBox;
class QSlider;
class QCheckBox;
class QPlainTextEdit;
class QTimer;
class QProgressBar;

namespace tiktool {

class AudioCapture;
class WsClient;
class ApiClient;
class TrialTracker;
class OverlayInstaller;

// Vertical glassmorphic dock. Compact, streamer-grade. Splits into:
//   * status (minutes left + state pill + buy/login CTA)
//   * audio (source picker, level meter, master toggle)
//   * language (source + translate-to)
//   * style (font size, font color, bg opacity, position, max lines, shadow, uppercase)
//   * live preview (rolling transcript)
//   * footer (open overlay URL / install to scene / wizard re-launch)
class TikToolCaptionsDock : public QDockWidget {
    Q_OBJECT
public:
    static TikToolCaptionsDock *install();
    void shutdown();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAudioPickerChanged(int);
    void onLanguageChanged();
    void onStyleChanged();
    void onMasterToggle();
    void onInstallOverlay();
    void onLoginClicked();
    void onTopupClicked();
    void onWizardClicked();
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
    void rebuildAudioSources();
    void pushSettingsToServer();
    void persistAndRestream();

    AudioCapture     *audio_     = nullptr;
    WsClient         *ws_        = nullptr;
    ApiClient        *api_       = nullptr;
    TrialTracker     *trial_     = nullptr;
    OverlayInstaller *installer_ = nullptr;

    // Status
    QLabel       *statusPill_  = nullptr;
    QLabel       *minutesLabel_= nullptr;
    QPushButton  *primaryCta_  = nullptr;
    QPushButton  *masterBtn_   = nullptr;
    QProgressBar *level_       = nullptr;

    // Audio
    QComboBox    *audioPicker_ = nullptr;

    // Language
    QComboBox    *srcLang_     = nullptr;
    QComboBox    *toLang_      = nullptr;

    // Style
    QSlider      *fontSize_    = nullptr;
    QComboBox    *fontColor_   = nullptr;
    QSlider      *bgOpacity_   = nullptr;
    QSlider      *position_    = nullptr;
    QSlider      *maxLines_    = nullptr;
    QCheckBox    *shadow_      = nullptr;
    QCheckBox    *uppercase_   = nullptr;
    QComboBox    *align_       = nullptr;

    // Preview
    QPlainTextEdit *transcript_ = nullptr;

    // Throttle saves
    QTimer       *saveDebounce_ = nullptr;

    bool streaming_ = false;
};

} // namespace tiktool
