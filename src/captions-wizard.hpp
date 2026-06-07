#pragma once

#include <QWizard>

class QComboBox;
class QLabel;

namespace tiktok {

class AudioCapture;
class WsClient;
class ApiClient;
class TrialTracker;
class OverlayInstaller;

// First-run hand-holding wizard. Five short steps:
//   1. Welcome + free-trial explainer
//   2. Pick a microphone / OBS audio source
//   3. Pick source + translation language
//   4. Demo: 30-second test with real captions overlay rendered in OBS preview
//   5. Done -> hands control back to the dock and offers sign-in if anonymous
class CaptionsWizard : public QWizard {
    Q_OBJECT
public:
    explicit CaptionsWizard(QWidget *parent = nullptr);

signals:
    void completed();

private:
    void buildWelcomePage();
    void buildUsernamePage();
    void buildAudioPage();
    void buildLanguagePage();
    void buildDemoPage();
    void buildTiktokStudioPage();
    void buildDonePage();
};

} // namespace tiktok
