#include "captions-dock.hpp"
#include "audio-capture.hpp"
#include "ws-client.hpp"
#include "api-client.hpp"
#include "trial-tracker.hpp"
#include "overlay-installer.hpp"
#include "settings.hpp"
#include "captions-wizard.hpp"
#include "version.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace tiktool {

static const QStringList LANG_CODES = {
    "auto", "en", "es", "pt", "fr", "de", "it", "tr", "ru", "ar", "ja", "ko", "zh"
};
static const QStringList LANG_NAMES = {
    "Auto-detect", "English", "Spanish", "Portuguese", "French", "German",
    "Italian", "Turkish", "Russian", "Arabic", "Japanese", "Korean", "Chinese"
};
static const QStringList TRANSLATE_CODES = {
    "off", "en", "es", "pt", "fr", "de", "it", "tr", "ru", "ar", "ja", "ko", "zh"
};
static const QStringList TRANSLATE_NAMES = {
    "No translation", "to English", "to Spanish", "to Portuguese",
    "to French", "to German", "to Italian", "to Turkish", "to Russian",
    "to Arabic", "to Japanese", "to Korean", "to Chinese"
};

TikToolCaptionsDock *TikToolCaptionsDock::install() {
    auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!mainWin) return nullptr;
    auto *dock = new TikToolCaptionsDock();
    dock->setParent(mainWin);

    // OBS 30+ exposes obs_frontend_add_dock_by_id. We target 30+ so this
    // is the only supported registration path; the legacy obs_frontend_add_dock
    // was removed in 32.
    obs_frontend_add_dock_by_id("tiktool-captions",
                                obs_module_text("TikToolCaptions"), dock);
    return dock;
}

void TikToolCaptionsDock::shutdown() {
    if (ws_) ws_->disconnectWs();
    if (audio_) audio_->unbind();
    deleteLater();
}

TikToolCaptionsDock::TikToolCaptionsDock()
    : QDockWidget(QStringLiteral("TikTool Live Captions")) {
    setObjectName("TikToolCaptionsDock");
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    api_       = new ApiClient(this);
    api_->setJwt(Settings::instance().jwt());
    api_->setApiKey(Settings::instance().apiKey());

    audio_     = new AudioCapture(this);
    ws_        = new WsClient(this);
    trial_     = new TrialTracker(api_, this);
    installer_ = new OverlayInstaller(this);

    buildUi();
    applyStyle();

    saveDebounce_ = new QTimer(this);
    saveDebounce_->setSingleShot(true);
    saveDebounce_->setInterval(750);
    connect(saveDebounce_, &QTimer::timeout, this, &TikToolCaptionsDock::pushSettingsToServer);

    audio_->setFrameCallback([this](const uint8_t *data, size_t bytes) {
        if (!ws_) return;
        ws_->sendAudio(QByteArray(reinterpret_cast<const char *>(data), int(bytes)));
    });

    connect(audio_, &AudioCapture::levelMeter, this, &TikToolCaptionsDock::onLevelMeter);
    connect(ws_, &WsClient::connectedChanged, this, &TikToolCaptionsDock::onWsConnectedChanged);
    connect(ws_, &WsClient::partial,    this, &TikToolCaptionsDock::onPartial);
    connect(ws_, &WsClient::finalLine,  this, &TikToolCaptionsDock::onFinal);
    connect(ws_, &WsClient::minutesUpdate, this, &TikToolCaptionsDock::onMinutesUpdate);
    connect(trial_, &TrialTracker::changed, this, &TikToolCaptionsDock::onTrialChanged);

    trial_->refresh();

    if (!Settings::instance().wizardCompleted())
        QTimer::singleShot(400, this, &TikToolCaptionsDock::onWizardClicked);
}

void TikToolCaptionsDock::buildUi() {
    auto *root = new QWidget(this);
    root->setObjectName("ttcRoot");
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(root);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    setWidget(scroll);
    setMinimumWidth(360);

    auto *v = new QVBoxLayout(root);
    v->setContentsMargins(18, 18, 18, 18);
    v->setSpacing(14);

    auto makeCard = [&](const QString &title) {
        auto *card = new QFrame();
        card->setObjectName("ttcCard");
        auto *vv = new QVBoxLayout(card);
        vv->setContentsMargins(16, 14, 16, 14);
        vv->setSpacing(10);
        auto *t = new QLabel(title);
        t->setObjectName("ttcCardTitle");
        vv->addWidget(t);
        return std::pair<QFrame *, QVBoxLayout *>(card, vv);
    };

    // Status card
    {
        auto [card, vv] = makeCard("STATUS");
        auto *row = new QHBoxLayout();
        statusPill_ = new QLabel("Connecting...");
        statusPill_->setObjectName("ttcPill");
        minutesLabel_ = new QLabel("- min left");
        minutesLabel_->setObjectName("ttcMinutes");
        row->addWidget(statusPill_);
        row->addStretch();
        row->addWidget(minutesLabel_);
        vv->addLayout(row);

        level_ = new QProgressBar();
        level_->setRange(0, 100);
        level_->setValue(0);
        level_->setTextVisible(false);
        level_->setObjectName("ttcLevel");
        vv->addWidget(level_);

        auto *cta = new QHBoxLayout();
        primaryCta_ = new QPushButton("Sign in with tik.tools");
        primaryCta_->setObjectName("ttcPrimary");
        masterBtn_  = new QPushButton("Start captions");
        masterBtn_->setObjectName("ttcMaster");
        masterBtn_->setCheckable(true);
        connect(primaryCta_, &QPushButton::clicked, this, [this]() {
            if (Settings::instance().jwt().isEmpty())
                onLoginClicked();
            else
                onTopupClicked();
        });
        connect(masterBtn_,  &QPushButton::clicked, this, &TikToolCaptionsDock::onMasterToggle);
        cta->addWidget(primaryCta_);
        cta->addWidget(masterBtn_);
        vv->addLayout(cta);

        v->addWidget(card);
    }

    // Audio card
    {
        auto [card, vv] = makeCard("MIC / AUDIO");
        audioPicker_ = new QComboBox();
        rebuildAudioSources();
        connect(audioPicker_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onAudioPickerChanged);
        vv->addWidget(audioPicker_);
        auto *hint = new QLabel("We listen to the audio you already route through OBS - no extra mic driver needed.");
        hint->setObjectName("ttcHint");
        hint->setWordWrap(true);
        vv->addWidget(hint);
        v->addWidget(card);
    }

    // Language card
    {
        auto [card, vv] = makeCard("LANGUAGE");
        srcLang_ = new QComboBox();
        for (int i = 0; i < LANG_CODES.size(); ++i)
            srcLang_->addItem(LANG_NAMES[i], LANG_CODES[i]);
        toLang_  = new QComboBox();
        for (int i = 0; i < TRANSLATE_CODES.size(); ++i)
            toLang_->addItem(TRANSLATE_NAMES[i], TRANSLATE_CODES[i]);

        auto cur = Settings::instance().language();
        int ix = LANG_CODES.indexOf(cur.sourceLanguage); if (ix >= 0) srcLang_->setCurrentIndex(ix);
        int tx = TRANSLATE_CODES.indexOf(cur.translateTo); if (tx >= 0) toLang_->setCurrentIndex(tx);

        connect(srcLang_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onLanguageChanged);
        connect(toLang_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onLanguageChanged);

        auto *row = new QHBoxLayout();
        row->addWidget(srcLang_);
        row->addWidget(toLang_);
        vv->addLayout(row);

        auto *warn = new QLabel("Translation burns 2 min per minute (1 source + 1 translation).");
        warn->setObjectName("ttcHint");
        warn->setWordWrap(true);
        vv->addWidget(warn);
        v->addWidget(card);
    }

    // Style card
    {
        auto [card, vv] = makeCard("STYLE");
        auto cur = Settings::instance().style();

        auto addSlider = [&](const QString &label, int min, int max, int val) {
            auto *row = new QHBoxLayout();
            auto *l = new QLabel(label);
            l->setObjectName("ttcRowLabel");
            l->setMinimumWidth(96);
            auto *s = new QSlider(Qt::Horizontal);
            s->setRange(min, max);
            s->setValue(val);
            row->addWidget(l);
            row->addWidget(s);
            vv->addLayout(row);
            return s;
        };

        fontSize_  = addSlider("Font size", 18, 120, cur.fontSize);
        bgOpacity_ = addSlider("BG opacity", 0, 100, cur.bgOpacity);
        position_  = addSlider("Position",  0, 100, cur.position);
        maxLines_  = addSlider("Max lines", 1, 5,   cur.maxLines);

        align_ = new QComboBox();
        align_->addItem("Left",   "left");
        align_->addItem("Center", "center");
        align_->addItem("Right",  "right");
        int aix = align_->findData(cur.align); if (aix >= 0) align_->setCurrentIndex(aix);

        fontColor_ = new QComboBox();
        const std::pair<QString, QString> palette[] = {
            {"White", "#FFFFFF"}, {"Black", "#000000"}, {"Yellow", "#FFD600"},
            {"Pink",  "#FF5BB3"}, {"Cyan",  "#00E0FF"}, {"Mint",  "#00FFA3"}
        };
        for (auto &p : palette) fontColor_->addItem(p.first, p.second);
        int cix = fontColor_->findData(cur.fontColor.toUpper()); if (cix >= 0) fontColor_->setCurrentIndex(cix);

        shadow_    = new QCheckBox("Shadow");      shadow_->setChecked(cur.shadow);
        uppercase_ = new QCheckBox("UPPERCASE");   uppercase_->setChecked(cur.uppercase);

        auto *row1 = new QHBoxLayout();
        row1->addWidget(new QLabel("Align"));
        row1->addWidget(align_);
        row1->addWidget(new QLabel("Color"));
        row1->addWidget(fontColor_);
        vv->addLayout(row1);

        auto *row2 = new QHBoxLayout();
        row2->addWidget(shadow_);
        row2->addWidget(uppercase_);
        row2->addStretch();
        vv->addLayout(row2);

        for (auto *sl : {fontSize_, bgOpacity_, position_, maxLines_})
            connect(sl, &QSlider::valueChanged, this, &TikToolCaptionsDock::onStyleChanged);
        connect(align_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onStyleChanged);
        connect(fontColor_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onStyleChanged);
        connect(shadow_,    &QCheckBox::toggled, this, &TikToolCaptionsDock::onStyleChanged);
        connect(uppercase_, &QCheckBox::toggled, this, &TikToolCaptionsDock::onStyleChanged);

        v->addWidget(card);
    }

    // Preview card
    {
        auto [card, vv] = makeCard("LIVE TRANSCRIPT");
        transcript_ = new QPlainTextEdit();
        transcript_->setReadOnly(true);
        transcript_->setPlaceholderText("Captions will appear here while you speak.");
        transcript_->setObjectName("ttcTranscript");
        transcript_->setMinimumHeight(160);
        vv->addWidget(transcript_);
        v->addWidget(card);
    }

    // Footer
    {
        auto *footer = new QHBoxLayout();
        auto *install = new QPushButton("Install overlay in scene");
        install->setObjectName("ttcFooterBtn");
        auto *wizard = new QPushButton("Re-run wizard");
        wizard->setObjectName("ttcFooterBtn");
        connect(install, &QPushButton::clicked, this, &TikToolCaptionsDock::onInstallOverlay);
        connect(wizard,  &QPushButton::clicked, this, &TikToolCaptionsDock::onWizardClicked);
        footer->addWidget(install);
        footer->addWidget(wizard);
        v->addLayout(footer);
    }

    v->addStretch();
}

void TikToolCaptionsDock::applyStyle() {
    QFile f(":/tiktool/style.qss");
    if (f.open(QIODevice::ReadOnly)) {
        setStyleSheet(QString::fromUtf8(f.readAll()));
    } else {
        // Fallback inline QSS keeps the dock usable even if the resource bundle
        // failed to ship. Glassmorphism is approximated since Qt has no real
        // backdrop-filter; we lean on translucent surfaces + thin borders.
        setStyleSheet(R"(
            QDockWidget { background: transparent; }
            #ttcRoot { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(20,18,30,0.96), stop:1 rgba(10,10,16,0.96)); color: #fff; }
            #ttcCard { background: rgba(255,255,255,0.04); border-radius: 16px;
                border: 1px solid rgba(255,255,255,0.08); }
            #ttcCardTitle { font-weight: 800; letter-spacing: 0.08em;
                color: #b8b6c7; font-size: 11px; }
            #ttcPill { background: rgba(0,255,163,0.15); color: #00ffa3;
                padding: 4px 10px; border-radius: 999px; font-size: 11px;
                font-weight: 700; letter-spacing: 0.05em; }
            #ttcMinutes { color: #fff; font-weight: 700; }
            #ttcPrimary { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #a74ffd, stop:1 #4b67f0); color: #fff; border: 0;
                padding: 10px 14px; border-radius: 10px; font-weight: 700; }
            #ttcPrimary:hover { opacity: 0.95; }
            #ttcMaster { background: rgba(255,255,255,0.08); color: #fff;
                border: 1px solid rgba(255,255,255,0.12); padding: 10px 14px;
                border-radius: 10px; font-weight: 700; }
            #ttcMaster:checked { background: #ff5b9c; border-color: #ff5b9c; }
            #ttcFooterBtn { background: rgba(255,255,255,0.06); color: #fff;
                border: 1px solid rgba(255,255,255,0.1); border-radius: 10px;
                padding: 8px 12px; }
            #ttcLevel { background: rgba(255,255,255,0.04); border: 0; height: 6px; }
            #ttcLevel::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #00ffa3, stop:1 #a74ffd); border-radius: 3px; }
            #ttcHint { color: #9a98ab; font-size: 11px; }
            #ttcRowLabel { color: #cfcde0; font-size: 12px; }
            #ttcTranscript { background: rgba(0,0,0,0.35); color: #fff;
                border: 1px solid rgba(255,255,255,0.08); border-radius: 12px;
                padding: 10px; }
            QComboBox, QSlider, QPushButton, QCheckBox { color: #fff; }
            QComboBox { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1);
                padding: 6px 8px; border-radius: 8px; }
            QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.1); border-radius: 2px; }
            QSlider::handle:horizontal { width: 14px; height: 14px; background: #fff;
                margin: -6px 0; border-radius: 7px; }
        )");
    }
}

void TikToolCaptionsDock::rebuildAudioSources() {
    if (!audioPicker_) return;
    QString prev = audioPicker_->currentText();
    if (prev.isEmpty()) prev = Settings::instance().audioSourceName();
    audioPicker_->blockSignals(true);
    audioPicker_->clear();
    auto list = AudioCapture::enumerateAudioSources();
    if (list.isEmpty()) audioPicker_->addItem("<no audio sources found>");
    else                audioPicker_->addItems(list);
    int ix = audioPicker_->findText(prev);
    if (ix >= 0) audioPicker_->setCurrentIndex(ix);
    audioPicker_->blockSignals(false);
}

void TikToolCaptionsDock::closeEvent(QCloseEvent *e) {
    QDockWidget::closeEvent(e);
}

void TikToolCaptionsDock::onAudioPickerChanged(int) {
    if (!audioPicker_) return;
    const QString name = audioPicker_->currentText();
    Settings::instance().setAudioSourceName(name);
    if (audio_) audio_->bindToSource(name);
}

void TikToolCaptionsDock::onLanguageChanged() {
    LanguageConfig lc;
    lc.sourceLanguage = srcLang_->currentData().toString();
    lc.translateTo    = toLang_->currentData().toString();
    Settings::instance().setLanguage(lc);
    if (trial_) trial_->setTranslating(lc.translateTo != "off");
    persistAndRestream();
}

void TikToolCaptionsDock::onStyleChanged() {
    CaptionStyle s = Settings::instance().style();
    s.fontSize   = fontSize_->value();
    s.bgOpacity  = bgOpacity_->value();
    s.position   = position_->value();
    s.maxLines   = maxLines_->value();
    s.align      = align_->currentData().toString();
    s.fontColor  = fontColor_->currentData().toString();
    s.shadow     = shadow_->isChecked();
    s.uppercase  = uppercase_->isChecked();
    Settings::instance().setStyle(s);
    saveDebounce_->start();
}

void TikToolCaptionsDock::onMasterToggle() {
    streaming_ = masterBtn_->isChecked();
    masterBtn_->setText(streaming_ ? "Stop captions" : "Start captions");
    if (streaming_) {
        if (audio_->boundSourceName().isEmpty()) onAudioPickerChanged(0);
        audio_->setStreaming(true);
        auto lc = Settings::instance().language();
        auto sc = Settings::instance().session();
        const QString mode = (lc.translateTo != "off") ? "translate" : sc.mode;
        ws_->connectWith(Settings::instance().jwt(),
                         Settings::instance().deviceId(),
                         mode, lc.sourceLanguage, lc.translateTo);
    } else {
        audio_->setStreaming(false);
        ws_->disconnectWs();
    }
}

void TikToolCaptionsDock::onInstallOverlay() {
    api_->mintOverlayToken([this](bool ok, const QJsonObject &body, int) {
        if (!ok) {
            statusPill_->setText("Overlay token failed");
            return;
        }
        const QString token = body.value("token").toString(body.value("jwt").toString());
        const QString setupId = body.value("setupId").toString();
        const QString url = installer_->installOrUpdate(
            token, setupId, Settings::instance().style(), Settings::instance().language());
        if (!url.isEmpty()) {
            statusPill_->setText("Overlay installed");
        }
    });
}

void TikToolCaptionsDock::onLoginClicked() {
    const QString url = QString::fromLatin1(TIKTOOL_WEB_BASE)
        + "/login?from=obs-plugin&device=" + Settings::instance().deviceId()
        + "&return=" + QUrl::toPercentEncoding(
              QString::fromLatin1(TIKTOOL_WEB_BASE) + "/captions/plugin-link");
    QDesktopServices::openUrl(QUrl(url));
    statusPill_->setText("Finish sign-in in your browser...");
    // The web page POSTs back a one-time linkToken; we poll for it.
    auto *poll = new QTimer(this);
    poll->setInterval(3000);
    QObject::connect(poll, &QTimer::timeout, this, [this, poll]() {
        api_->checkTrial(Settings::instance().deviceId(), [this, poll]
            (bool ok, const QJsonObject &b, int) {
            if (!ok) return;
            if (b.contains("linkToken")) {
                trial_->linkDevice(b.value("linkToken").toString());
                poll->stop();
                poll->deleteLater();
            }
        });
    });
    poll->start();
}

void TikToolCaptionsDock::onTopupClicked() {
    QString url = QString::fromLatin1(TIKTOOL_WEB_BASE) + "/captions/watch?topup=1";
    if (!Settings::instance().jwt().isEmpty())
        url += "&device=" + Settings::instance().deviceId();
    QDesktopServices::openUrl(QUrl(url));
}

void TikToolCaptionsDock::onWizardClicked() {
    rebuildAudioSources();
    auto *wiz = new CaptionsWizard(this);
    QObject::connect(wiz, &CaptionsWizard::completed, this, [this]() {
        Settings::instance().setWizardCompleted(true);
        Settings::instance().save();
        if (audioPicker_) audioPicker_->setCurrentText(Settings::instance().audioSourceName());
        // Drop the user straight into a live state if they already have
        // minutes available.
        if (trial_ && trial_->minutesLeft() > 0 && !streaming_) {
            masterBtn_->setChecked(true);
            onMasterToggle();
        }
    });
    wiz->setAttribute(Qt::WA_DeleteOnClose);
    wiz->show();
}

void TikToolCaptionsDock::onTrialChanged() {
    const auto state = trial_->state();
    const int mins   = trial_->minutesLeft();
    minutesLabel_->setText(QString("%1 min left").arg(mins));
    switch (state) {
        case TrialTracker::State::Paid:
            statusPill_->setText("Paid plan");
            primaryCta_->setText("Top up minutes");
            break;
        case TrialTracker::State::AccountTrial:
            statusPill_->setText("Account trial");
            primaryCta_->setText("Top up minutes");
            break;
        case TrialTracker::State::AccountExpired:
            statusPill_->setText("Out of minutes");
            primaryCta_->setText("Top up minutes");
            break;
        case TrialTracker::State::AnonymousTrial:
            statusPill_->setText("Free trial");
            primaryCta_->setText("Sign in for 10 more min");
            break;
        case TrialTracker::State::AnonymousExpired:
            statusPill_->setText("Trial used up");
            primaryCta_->setText("Sign in with tik.tools");
            break;
        default:
            statusPill_->setText("Connecting...");
            break;
    }
}

void TikToolCaptionsDock::onWsConnectedChanged(bool c) {
    statusPill_->setText(c ? "Live" : "Connecting...");
}

void TikToolCaptionsDock::onPartial(const QString &text, const QString &) {
    if (!transcript_ || text.isEmpty()) return;
    // Render partials as last-line in italics; we just append and let the
    // scroller follow. Keep last ~120 paragraphs to bound memory.
    transcript_->appendPlainText(QString("... %1").arg(text));
    if (transcript_->blockCount() > 240) {
        QTextCursor cur(transcript_->document());
        cur.movePosition(QTextCursor::Start);
        cur.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor,
                         transcript_->blockCount() - 240);
        cur.removeSelectedText();
    }
}

void TikToolCaptionsDock::onFinal(const QString &text, const QString &,
                                  const QString &translation, const QString &) {
    if (!transcript_ || text.isEmpty()) return;
    transcript_->appendPlainText(text);
    if (!translation.isEmpty())
        transcript_->appendPlainText(QString("  -> %1").arg(translation));
}

void TikToolCaptionsDock::onMinutesUpdate(int m) {
    if (trial_) trial_->noteMinutesFromServer(m);
}

void TikToolCaptionsDock::onLevelMeter(double dbfs) {
    if (!level_) return;
    // Map -60..0 dBFS to 0..100
    int v = int(std::max(0.0, std::min(100.0, (dbfs + 60.0) / 60.0 * 100.0)));
    level_->setValue(v);
}

void TikToolCaptionsDock::pushSettingsToServer() {
    Settings::instance().save();
    if (Settings::instance().jwt().isEmpty()) return;
    QJsonObject body;
    auto st = Settings::instance().style();
    QJsonObject style;
    style["fontSize"] = st.fontSize; style["fontFamily"] = st.fontFamily;
    style["fontColor"] = st.fontColor; style["bgColor"] = st.bgColor;
    style["bgOpacity"] = st.bgOpacity; style["shadow"] = st.shadow;
    style["uppercase"] = st.uppercase; style["align"] = st.align;
    style["position"] = st.position; style["maxLines"] = st.maxLines;
    style["lineHeight"] = st.lineHeight;
    body["style"] = style;

    auto lc = Settings::instance().language();
    QJsonObject lang;
    lang["sourceLanguage"] = lc.sourceLanguage; lang["translateTo"] = lc.translateTo;
    body["language"] = lang;

    body["source"] = "obs-plugin";
    api_->saveSetup({}, body, {});
}

void TikToolCaptionsDock::persistAndRestream() {
    pushSettingsToServer();
    if (streaming_) {
        // Reconnect WS so the language switch takes effect server-side.
        ws_->disconnectWs();
        auto lc = Settings::instance().language();
        auto sc = Settings::instance().session();
        const QString mode = (lc.translateTo != "off") ? "translate" : sc.mode;
        ws_->connectWith(Settings::instance().jwt(),
                         Settings::instance().deviceId(),
                         mode, lc.sourceLanguage, lc.translateTo);
    }
}

} // namespace tiktool
