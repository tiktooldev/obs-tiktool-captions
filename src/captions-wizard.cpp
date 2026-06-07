#include "captions-wizard.hpp"
#include "audio-capture.hpp"
#include "settings.hpp"
#include "overlay-installer.hpp"
#include "scene-setup.hpp"
#include "version.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QTimer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizardPage>

namespace tiktok {

CaptionsWizard::CaptionsWizard(QWidget *parent) : QWizard(parent) {
    setWindowTitle("TikTok Captions - by TikTools - Setup");
    setMinimumSize(560, 600);
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::IgnoreSubTitles, false);

    buildWelcomePage();
    buildUsernamePage();
    buildAudioPage();
    buildLanguagePage();
    buildDemoPage();
    buildTiktokStudioPage();
    buildDonePage();

    connect(this, &QWizard::finished, this, [this](int) { emit completed(); });
}

static QWizardPage *makePage(const QString &title, const QString &subtitle, QWidget *content) {
    auto *p = new QWizardPage();
    p->setTitle(title);
    p->setSubTitle(subtitle);
    auto *v = new QVBoxLayout(p);
    v->addWidget(content);
    return p;
}

void CaptionsWizard::buildWelcomePage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    auto *body = new QLabel(
        "<p style='font-size:14px;line-height:1.6'>"
        "Real-time captions for your TikTok LIVE - vertical, beautiful, no extra mic driver.<br><br>"
        "<b>Sign in with your tik.tools account to unlock 60 free trial minutes.</b> "
        "Translation burns 2 minutes per real minute (1 for source + 1 for translation). "
        "After the trial, pick a captions plan at tik.tools/captions - Casual, Pro or Extreme, "
        "weekly or monthly. Plans remove the watermark and give you hours of stream time.<br><br>"
        "We listen to whatever audio you already route through OBS, send it straight to "
        "tik.tools' caption engine, and overlay the result on your scene at 9:16."
        "</p>");
    body->setWordWrap(true);
    v->addWidget(body);
    addPage(makePage("Welcome to TikTok Live Captions - by TikTools",
                     "Three minutes to set up. Streamer-grade output.", w));
}

// Wizard page that refuses to advance until the streamer types a valid
// TikTok handle. The handle is required so the captions hub keys on it -
// without it the overlay does not know whose audio session it is rendering.
class UsernameWizardPage : public QWizardPage {
public:
    UsernameWizardPage() {
        setTitle("Your TikTok @username");
        setSubTitle("So we can tag your captions hub. Required.");

        auto *v = new QVBoxLayout(this);
        auto *info = new QLabel(
            "<p style='font-size:13px;line-height:1.55'>"
            "Type the @username of the TikTok account you stream from. "
            "Captions are scoped to your account so the overlay knows whose audio "
            "is being rendered."
            "<br><br>No @ prefix - just the handle. Letters, digits, dot and underscore allowed.</p>");
        info->setWordWrap(true);
        v->addWidget(info);

        edit_ = new QLineEdit();
        edit_->setPlaceholderText("yourhandle");
        edit_->setMaxLength(40);
        edit_->setText(Settings::instance().tiktokUsername());
        edit_->setStyleSheet("padding:8px 10px;font-size:14px;");
        v->addWidget(edit_);

        hint_ = new QLabel("");
        hint_->setStyleSheet("color:#9ad;font-size:11px;");
        v->addWidget(hint_);

        connect(edit_, &QLineEdit::textChanged, this, &UsernameWizardPage::onChanged);
        onChanged();
    }
    bool isComplete() const override { return ok_; }

private slots:
    void onChanged() {
        QString u = edit_->text().trimmed();
        if (u.startsWith('@')) u = u.mid(1);
        u = u.toLower();
        const QRegularExpression re("^[a-zA-Z0-9._]{2,40}$");
        ok_ = re.match(u).hasMatch();
        if (ok_) {
            Settings::instance().setTiktokUsername(u);
            Settings::instance().save();
            hint_->setText("Saved as @" + u);
            hint_->setStyleSheet("color:#7ce17c;font-size:11px;");
        } else {
            hint_->setText(u.isEmpty() ? "" : "Invalid handle - 2-40 chars: letters, digits, dot, underscore");
            hint_->setStyleSheet("color:#ff7b7b;font-size:11px;");
        }
        emit completeChanged();
    }

private:
    QLineEdit *edit_ = nullptr;
    QLabel    *hint_ = nullptr;
    bool       ok_   = false;
};

void CaptionsWizard::buildUsernamePage() {
    addPage(new UsernameWizardPage());
}

void CaptionsWizard::buildAudioPage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    auto *info = new QLabel("Pick a microphone. We list every physical mic / line-in / loopback "
                            "device your OS exposes, plus any audio source you have already added "
                            "in OBS.");
    info->setWordWrap(true);
    v->addWidget(info);

    auto *combo = new QComboBox();

    // System devices (the same enum the dock uses). Payload is a QStringList
    // {mode, deviceId, displayName} so we know how to bind on Finish.
    const auto devices = AudioCapture::enumerateSystemDevices();
    if (!devices.isEmpty()) {
        combo->addItem("--- System audio devices ---");
        for (const auto &d : devices)
            combo->addItem(d.displayName, QVariant::fromValue(QStringList{ "system", d.id, d.displayName }));
    }
    const auto sources = AudioCapture::enumerateAudioSources();
    if (!sources.isEmpty()) {
        combo->addItem("--- OBS audio sources ---");
        for (const auto &s : sources)
            combo->addItem(s, QVariant::fromValue(QStringList{ "obs-source", QString(), s }));
    }
    if (combo->count() == 0) combo->addItem("<no audio devices found>");

    // Restore the user's last pick.
    const QString prevMode   = Settings::instance().audioMode();
    const QString prevDevice = Settings::instance().audioDeviceId();
    const QString prevSource = Settings::instance().audioSourceName();
    for (int i = 0; i < combo->count(); ++i) {
        QStringList v2 = combo->itemData(i).toStringList();
        if (v2.size() < 3) continue;
        if (prevMode == "system" && v2[0] == "system" && v2[1] == prevDevice) { combo->setCurrentIndex(i); break; }
        if (prevMode == "obs-source" && v2[0] == "obs-source" && v2[2] == prevSource) { combo->setCurrentIndex(i); break; }
    }

    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [combo](int) {
        QStringList v2 = combo->currentData().toStringList();
        if (v2.size() < 3) return;
        Settings::instance().setAudioMode(v2[0]);
        Settings::instance().setAudioDeviceId(v2[1]);
        Settings::instance().setAudioSourceName(v2[2]);
        Settings::instance().save();
    });
    v->addWidget(combo);

    auto *hint = new QLabel("System devices are the cleanest path - no need to add anything in OBS first.");
    hint->setStyleSheet("color:#888;font-size:11px");
    hint->setWordWrap(true);
    v->addWidget(hint);

    addPage(makePage("Choose your mic",
                     "Pick any system mic or an existing OBS audio source.", w));

    // Co-host audio page - separate so we can have a clear opt-out + label.
    auto *cw = new QWidget();
    auto *cv = new QVBoxLayout(cw);
    auto *cinfo = new QLabel(
        "Optional: pick the audio device for your <b>co-host / battle opponent</b>. "
        "When set, we mix your mic + their voice + transcribe both together. "
        "Use the same device you listen to opponents from in TikTok LIVE Studio. "
        "<br><br>Solo streamers can leave this on <b>Off</b>.");
    cinfo->setWordWrap(true);
    cv->addWidget(cinfo);
    auto *ccombo = new QComboBox();
    ccombo->addItem("Off (solo stream)", QVariant::fromValue(QStringList{ "off", QString(), QString() }));
    const auto cDevices = AudioCapture::enumerateSystemDevices();
    for (const auto &d : cDevices) ccombo->addItem(d.displayName, QVariant::fromValue(QStringList{ "system", d.id, d.displayName }));
    const auto cSources = AudioCapture::enumerateAudioSources();
    for (const auto &s : cSources) ccombo->addItem(s, QVariant::fromValue(QStringList{ "obs-source", QString(), s }));
    const QString cm = Settings::instance().cohostMode();
    for (int i = 0; i < ccombo->count(); ++i) {
        QStringList v = ccombo->itemData(i).toStringList();
        if (v.size() < 3) continue;
        if (cm == "off" && v[0] == "off") { ccombo->setCurrentIndex(i); break; }
        if (cm == "system" && v[0] == "system" && v[1] == Settings::instance().cohostDeviceId()) { ccombo->setCurrentIndex(i); break; }
        if (cm == "obs-source" && v[0] == "obs-source" && v[2] == Settings::instance().cohostSourceName()) { ccombo->setCurrentIndex(i); break; }
    }
    QObject::connect(ccombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [ccombo](int) {
        QStringList v = ccombo->currentData().toStringList();
        if (v.size() < 3) return;
        Settings::instance().setCohostMode(v[0]);
        Settings::instance().setCohostDeviceId(v[1]);
        Settings::instance().setCohostSourceName(v[2]);
        Settings::instance().save();
    });
    cv->addWidget(ccombo);
    addPage(makePage("Co-host / battle audio",
                     "Translate your opponent's voice too. Optional.", cw));
}

void CaptionsWizard::buildLanguagePage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);

    // real-time multilingual list (synced with the dock).
    const QList<QPair<QString, QString>> langs = {
        {"auto", "Auto-detect"},
        {"ar", "Arabic"}, {"bg", "Bulgarian"}, {"ca", "Catalan"}, {"zh", "Chinese"},
        {"hr", "Croatian"}, {"cs", "Czech"}, {"da", "Danish"}, {"nl", "Dutch"},
        {"en", "English"}, {"et", "Estonian"}, {"fil", "Filipino (Tagalog)"},
        {"fi", "Finnish"}, {"fr", "French"}, {"gl", "Galician"}, {"de", "German"},
        {"el", "Greek"}, {"he", "Hebrew"}, {"hi", "Hindi"}, {"hu", "Hungarian"},
        {"id", "Indonesian"}, {"it", "Italian"}, {"ja", "Japanese"}, {"ko", "Korean"},
        {"lv", "Latvian"}, {"lt", "Lithuanian"}, {"ms", "Malay"}, {"no", "Norwegian"},
        {"pl", "Polish"}, {"pt", "Portuguese"}, {"ro", "Romanian"}, {"ru", "Russian"},
        {"sr", "Serbian"}, {"sk", "Slovak"}, {"sl", "Slovenian"}, {"es", "Spanish"},
        {"sv", "Swedish"}, {"ta", "Tamil"}, {"th", "Thai"}, {"tr", "Turkish"},
        {"uk", "Ukrainian"}, {"vi", "Vietnamese"},
    };

    auto *src = new QComboBox();
    for (const auto &p : langs) src->addItem(p.second, p.first);

    auto *trg = new QComboBox();
    trg->addItem("No translation", "off");
    for (const auto &p : langs) {
        if (p.first == "auto") continue;
        trg->addItem("to " + p.second, p.first);
    }

    auto cur = Settings::instance().language();
    int ix = src->findData(cur.sourceLanguage); if (ix >= 0) src->setCurrentIndex(ix);
    int tx = trg->findData(cur.translateTo);    if (tx >= 0) trg->setCurrentIndex(tx);

    auto applyAndSave = [src, trg]() {
        LanguageConfig lc;
        lc.sourceLanguage = src->currentData().toString();
        lc.translateTo    = trg->currentData().toString();
        Settings::instance().setLanguage(lc);
    };
    QObject::connect(src, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [applyAndSave](int){ applyAndSave(); });
    QObject::connect(trg, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [applyAndSave](int){ applyAndSave(); });

    auto *row = new QHBoxLayout();
    auto *l1 = new QVBoxLayout(); l1->addWidget(new QLabel("Source language")); l1->addWidget(src);
    auto *l2 = new QVBoxLayout(); l2->addWidget(new QLabel("Translate to"));    l2->addWidget(trg);
    row->addLayout(l1); row->addLayout(l2);
    v->addLayout(row);

    auto *warn = new QLabel("Heads up: translation burns 2 minutes per real minute "
                            "(one for the source language, one for the translation).");
    warn->setStyleSheet("color:#aaa;font-size:12px");
    warn->setWordWrap(true);
    v->addWidget(warn);

    addPage(makePage("Pick a language",
                     "Auto-detect works for everything; pick a specific one for the lowest latency.", w));
}

void CaptionsWizard::buildDemoPage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    auto *body = new QLabel(
        "We'll install a 1080x1920 vertical browser source named "
        "<b>'TikTok Captions - by TikTools'</b> into your current scene and start a 30-second "
        "demo so you can see captions land on your stream before you go live."
        "<br><br>Pin or resize it however you like - we'll remember it next time.");
    body->setWordWrap(true);
    v->addWidget(body);

    auto *btn = new QPushButton("Install overlay now");
    QObject::connect(btn, &QPushButton::clicked, this, [this, btn]() {
        OverlayInstaller inst;
        const QString url = inst.installOrUpdate(
            Settings::instance().jwt(), {},
            Settings::instance().style(),
            Settings::instance().language(),
            /*watermark=*/true); // wizard demo always shows the trial watermark
        btn->setText(url.isEmpty() ? "Install failed - see status" : "Installed");
        btn->setDisabled(true);
    });
    v->addWidget(btn);
    addPage(makePage("Drop the overlay in your scene",
                     "Vertical 9:16 by default. Move it anywhere on your canvas.", w));
}

void CaptionsWizard::buildTiktokStudioPage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);

    auto *body = new QLabel(
        "<p style='font-size:13px;line-height:1.55'>"
        "Hit the big button below and we will:"
        "<br>&nbsp;&nbsp;1. Switch OBS to a vertical 1080x1920 canvas at 30 fps."
        "<br>&nbsp;&nbsp;2. Drop your webcam into the current scene if it is not there yet."
        "<br>&nbsp;&nbsp;3. Start the OBS Virtual Camera."
        "<br><br>Then open <b>TikTok LIVE Studio</b>, add a camera source, and pick "
        "<b>OBS Virtual Camera</b> as the device - exactly like the screenshot below."
        "</p>");
    body->setWordWrap(true);
    v->addWidget(body);

    auto *btn = new QPushButton("Auto-configure OBS for TikTok LIVE");
    btn->setStyleSheet("padding:10px 14px;font-weight:700;");
    auto *result = new QLabel("");
    result->setWordWrap(true);
    result->setStyleSheet("color:#9ad;font-size:12px;");
    QObject::connect(btn, &QPushButton::clicked, this, [btn, result]() {
        SceneSetup s;
        auto r = s.runAll();
        QStringList lines;
        lines << (r.canvasOk     ? "Canvas: 1080x1920 @ 30 fps."             : "Canvas: failed - stop active output first.");
        lines << (r.cameraOk     ? "Camera: present in current scene."      : "Camera: failed - check your webcam permissions.");
        lines << (r.virtualCamOk ? "Virtual Camera: running."               : "Virtual Camera: failed - open OBS Settings > Output.");
        if (!r.error.isEmpty()) lines << r.error;
        result->setText(lines.join("\n"));
        btn->setText(r.canvasOk && r.cameraOk && r.virtualCamOk
                     ? "All set - re-run if you want"
                     : "Try again");
    });
    v->addWidget(btn);
    v->addWidget(result);

    // Screenshot showing the TikTok LIVE Studio source picker so the streamer
    // sees the target UI state instead of guessing.
    auto *img = new QLabel();
    QPixmap pix(":/tiktok/tiktok-live-studio-setup.png");
    if (!pix.isNull()) {
        img->setPixmap(pix.scaledToWidth(520, Qt::SmoothTransformation));
    } else {
        img->setText("(setup screenshot missing - check Help > Logs)");
    }
    img->setAlignment(Qt::AlignCenter);
    v->addWidget(img);

    auto *cap = new QLabel(
        "<p style='font-size:11px;color:#888'>In TikTok LIVE Studio: <b>Add or edit a camera source</b> "
        "and select <b>OBS Virtual Camera</b> in the device dropdown.</p>");
    cap->setWordWrap(true);
    cap->setAlignment(Qt::AlignCenter);
    v->addWidget(cap);

    // Wrap in a scroll so the screenshot does not overflow on small screens.
    auto *scroll = new QScrollArea();
    scroll->setWidget(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *page = new QWizardPage();
    page->setTitle("One-click OBS setup");
    page->setSubTitle("We do the boring 1080x1920 + webcam + virtual cam dance for you.");
    auto *pv = new QVBoxLayout(page);
    pv->addWidget(scroll);
    addPage(page);
}

// Done page that refuses to Finish until the streamer has signed in. A QTimer
// polls Settings every 1 s for a JWT; when present, the button label flips +
// completeChanged() unlocks Finish.
class WizardDonePage : public QWizardPage {
public:
    WizardDonePage() {
        setTitle("All set");
        setSubTitle("Sign in to unlock your 60 free trial minutes.");
        auto *v = new QVBoxLayout(this);
        body_ = new QLabel(
            "<p style='font-size:13px;line-height:1.55'>"
            "Hit <b>Sign in with tik.tools</b> below to unlock 60 free trial minutes. "
            "After signing in, the button flips green and Finish unlocks. "
            "Translation burns 2 minutes per real minute. After the trial, pick a "
            "captions plan at <b>tik.tools/captions</b>.<br><br>"
            "Audio is streamed live to tik.tools for transcription. Never recorded."
            "</p>");
        body_->setWordWrap(true);
        v->addWidget(body_);
        signIn_ = new QPushButton("Sign in with tik.tools");
        connect(signIn_, &QPushButton::clicked, this, []() {
            const QString returnUrl = QString::fromLatin1(TIKTOOL_WEB_BASE)
                + "/captions/plugin-link?device=" + Settings::instance().deviceId()
                + "&fp=" + Settings::instance().fingerprint();
            const QString url = QString::fromLatin1(TIKTOOL_WEB_BASE)
                + "/login?from=obs-plugin&returnTo=" + QUrl::toPercentEncoding(returnUrl);
            QDesktopServices::openUrl(QUrl(url));
        });
        v->addWidget(signIn_);
        status_ = new QLabel("Not signed in yet.");
        status_->setStyleSheet("color:#ff9b9b;font-size:12px;");
        status_->setWordWrap(true);
        v->addWidget(status_);

        poll_ = new QTimer(this);
        poll_->setInterval(1000);
        connect(poll_, &QTimer::timeout, this, &WizardDonePage::tick);
        poll_->start();
        tick();
    }
    bool isComplete() const override { return authed_; }

private slots:
    void tick() {
        const bool wasAuthed = authed_;
        authed_ = !Settings::instance().jwt().isEmpty();
        if (authed_ && !wasAuthed) {
            signIn_->setText("Signed in - click Finish");
            signIn_->setStyleSheet("background:#1e7a4a;color:#fff;padding:10px 14px;font-weight:700;");
            status_->setText("Signed in as " + Settings::instance().accountEmail());
            status_->setStyleSheet("color:#7ce17c;font-size:12px;");
            emit completeChanged();
        } else if (!authed_) {
            // Status updates while waiting.
            const QString phase = signIn_->property("phase").toString();
            if (phase != "started") {
                signIn_->setProperty("phase", "started");
            }
        }
    }

private:
    QLabel       *body_   = nullptr;
    QPushButton  *signIn_ = nullptr;
    QLabel       *status_ = nullptr;
    QTimer       *poll_   = nullptr;
    bool          authed_ = false;
};

void CaptionsWizard::buildDonePage() {
    addPage(new WizardDonePage());
}

} // namespace tiktok
