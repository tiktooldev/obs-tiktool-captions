#include "captions-dock.hpp"
#include "audio-capture.hpp"
#include "ws-client.hpp"
#include "api-client.hpp"
#include "trial-tracker.hpp"
#include "overlay-installer.hpp"
#include "scene-setup.hpp"
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
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFontDatabase>
#include <QScrollArea>
#include <QSlider>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace tiktok {

// real-time multilingual STT supports 60+ languages. Synced with
// app/pages/captions/watch.vue + extended to match the STT engine published list.
// Codes are ISO 639-1 (or 639-3 where 639-1 does not exist, e.g. fil).
// We treat the source picker as { auto | 60 langs } and translate as
// { off | the same 60 langs }.
// Renders each combo entry in its own typeface so the streamer can preview
// the font without scrolling through a static name list. Falls back to a
// generic family if the font is not installed locally - the overlay loads
// it from Google Fonts at runtime, so it does not need to be on the OS.
class FontComboDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, idx);
        const QString family = idx.data(Qt::UserRole).toString();
        if (!family.isEmpty()) {
            QFont f = o.font;
            f.setFamily(family);
            f.setPointSize(qMax(11, o.font.pointSize() + 1));
            o.font = f;
        }
        QStyle *st = o.widget ? o.widget->style() : QApplication::style();
        st->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
    }
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(qMax(s.height(), 26));
        return s;
    }
};

static const QList<QPair<QString, QString>> LANGS = {
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

TikToolCaptionsDock *TikToolCaptionsDock::install() {
    auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!mainWin) return nullptr;
    auto *dock = new TikToolCaptionsDock();
    // No setParent() - OBS will reparent us into its own QDockWidget wrapper.
    // OBS uses the object name as the dock-state key for save / restore; must
    // be stable across releases so the geometry blob continues to match.
    dock->setObjectName(QStringLiteral("TikToolCaptionsDock"));
    obs_frontend_add_dock_by_id("tiktok-captions",
                                obs_module_text("TikToolCaptions"), dock);
    // Auto-show the dock the first time the streamer installs the plugin so
    // they do not have to hunt for it under View > Docks. OBS's
    // obs_frontend_add_dock_by_id wraps us in a QDockWidget that becomes our
    // Qt parent; queue a 0 ms slot so the parent chain is settled before we
    // raise it.
    QMetaObject::invokeMethod(dock, [dock]() {
        QWidget *p = dock->parentWidget();
        while (p) {
            if (auto *qdw = qobject_cast<QDockWidget *>(p)) {
                qdw->setVisible(true);
                qdw->raise();
                break;
            }
            p = p->parentWidget();
        }
    }, Qt::QueuedConnection);
    return dock;
}

void TikToolCaptionsDock::shutdown() {
    if (ws_) ws_->disconnectWs();
    if (audio_) audio_->unbind();
    // We do NOT deleteLater() ourselves - OBS's QDockWidget wrapper owns us
    // via Qt parent-child. It deletes us on its own teardown. Calling
    // deleteLater here used to race with OBS's frontend shutdown which
    // double-freed the dock on plugin unload.
}

TikToolCaptionsDock::TikToolCaptionsDock()
    : QFrame(nullptr) {
    setObjectName(QStringLiteral("TikToolCaptionsDockRoot"));
    setFrameShape(QFrame::NoFrame);

    api_       = new ApiClient(this);
    api_->setJwt(Settings::instance().jwt());
    api_->setApiKey(Settings::instance().apiKey());
    api_->setFingerprint(Settings::instance().fingerprint());

    audio_       = new AudioCapture(this);
    cohostAudio_ = new AudioCapture(this);
    ws_          = new WsClient(this);
    trial_     = new TrialTracker(api_, this);
    installer_ = new OverlayInstaller(this);

    buildUi();
    applyStyle();
    applyUiScale();

    saveDebounce_ = new QTimer(this);
    saveDebounce_->setSingleShot(true);
    saveDebounce_->setInterval(750);
    connect(saveDebounce_, &QTimer::timeout, this, &TikToolCaptionsDock::pushSettingsToServer);

    audio_->setFrameCallback([this](const uint8_t *data, size_t bytes) {
        if (!ws_) return;
        QByteArray frame(reinterpret_cast<const char *>(data), int(bytes));
        // Mix in the latest co-host PCM frame (if any, < 250 ms old). We
        // sum int16 samples + clamp to keep loudness reasonable.
        QByteArray cohost;
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        {
            std::lock_guard<std::mutex> g(cohostMutex_);
            if (!cohostFrame_.isEmpty() && nowMs - cohostFrameTs_ < 250)
                cohost = cohostFrame_;
        }
        if (!cohost.isEmpty() && cohost.size() == frame.size()) {
            int16_t       *a = reinterpret_cast<int16_t *>(frame.data());
            const int16_t *b = reinterpret_cast<const int16_t *>(cohost.constData());
            const int nSamples = frame.size() / 2;
            for (int i = 0; i < nSamples; ++i) {
                int32_t mixed = int32_t(a[i]) + int32_t(b[i]);
                if (mixed > 32767) mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                a[i] = int16_t(mixed);
            }
        }
        ws_->sendAudio(frame);
    });
    cohostAudio_->setFrameCallback([this](const uint8_t *data, size_t bytes) {
        QByteArray frame(reinterpret_cast<const char *>(data), int(bytes));
        std::lock_guard<std::mutex> g(cohostMutex_);
        cohostFrame_   = frame;
        cohostFrameTs_ = QDateTime::currentMSecsSinceEpoch();
    });

    connect(audio_, &AudioCapture::levelMeter, this, &TikToolCaptionsDock::onLevelMeter);
    connect(ws_, &WsClient::connectedChanged, this, &TikToolCaptionsDock::onWsConnectedChanged);
    connect(ws_, &WsClient::errorOccurred,    this, [this](const QString &reason) {
        if (!statusPill_) return;
        statusPill_->setText(reason.left(64));
    });
    connect(ws_, &WsClient::serverStatus,     this, [this](const QString &status) {
        if (!statusPill_) return;
        const QString user = Settings::instance().tiktokUsername();
        if (status == "waiting")
            statusPill_->setText(user.isEmpty()
                ? QString("Waiting for streamer to go live...")
                : QString("Waiting for @%1 to go live...").arg(user));
        else if (status == "connecting") statusPill_->setText("Connecting to stream...");
        else if (status == "live")       statusPill_->setText("Live - transcribing");
        else if (status == "transcribing") statusPill_->setText("Live - transcribing");
    });
    connect(ws_, &WsClient::partial,    this, &TikToolCaptionsDock::onPartial);
    connect(ws_, &WsClient::finalLine,  this, &TikToolCaptionsDock::onFinal);
    connect(ws_, &WsClient::minutesUpdate, this, &TikToolCaptionsDock::onMinutesUpdate);
    connect(trial_, &TrialTracker::changed, this, &TikToolCaptionsDock::onTrialChanged);

    trial_->refresh();

    if (!Settings::instance().wizardCompleted())
        QTimer::singleShot(400, this, &TikToolCaptionsDock::onWizardClicked);

    // Auto-resume captions on OBS startup if the streamer had them running
    // before quitting. Guarded by JWT presence + trial state; we never start
    // streaming for an account that has no minutes left or no sign-in. The
    // 4 s delay gives OBS time to fully register its audio sources, the
    // trial tracker time to land its first refresh, and the WS client a
    // gap to settle.
    if (Settings::instance().streamingPersisted()
        && !Settings::instance().jwt().isEmpty()) {
        QTimer::singleShot(4000, this, [this]() {
            if (!masterBtn_ || masterBtn_->isChecked()) return;
            if (!trial_) return;
            if (trial_->state() == TrialTracker::State::AccountExpired) return;
            if (trial_->state() == TrialTracker::State::DuplicateAccount) return;
            masterBtn_->setChecked(true);
            onMasterToggle();
        });
    }
}

void TikToolCaptionsDock::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *root = new QWidget(this);
    root->setObjectName("ttcRoot");
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(root);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    setMinimumWidth(360);

    auto *v = new QVBoxLayout(root);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    auto makeCard = [&](const QString &title) {
        auto *card = new QFrame();
        card->setObjectName("ttcCard");
        auto *vv = new QVBoxLayout(card);
        vv->setContentsMargins(12, 10, 12, 10);
        vv->setSpacing(6);
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
        // UI scale buttons - "A-" / "A+" let small-screen + large-screen users
        // resize the dock without leaving the plugin. Persisted via Settings.
        auto *sizeDown = new QPushButton(QString::fromUtf8("−"));   // minus sign
        auto *sizeUp   = new QPushButton(QString::fromUtf8("+"));
        sizeDown->setObjectName("ttcZoomBtn");
        sizeUp->setObjectName("ttcZoomBtn");
        sizeDown->setFixedSize(24, 24);
        sizeUp->setFixedSize(24, 24);
        sizeDown->setToolTip("Shrink dock UI");
        sizeUp->setToolTip("Enlarge dock UI");
        sizeDown->setStyleSheet("font-size:18px;font-weight:700;color:#fff;");
        sizeUp->setStyleSheet("font-size:18px;font-weight:700;color:#fff;");
        connect(sizeDown, &QPushButton::clicked, this, &TikToolCaptionsDock::onUiScaleSmaller);
        connect(sizeUp,   &QPushButton::clicked, this, &TikToolCaptionsDock::onUiScaleLarger);
        row->addWidget(statusPill_);
        row->addStretch();
        row->addWidget(sizeDown);
        row->addWidget(sizeUp);
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
        // Master button = the activate / deactivate toggle for the entire
        // pipeline: opens the WS, hooks the audio source, decrements minutes.
        // "Start captions" / "Stop captions" labels make the binary state
        // obvious without overloading the streamer with a third "active"
        // checkbox elsewhere.
        masterBtn_  = new QPushButton("Start captions");
        masterBtn_->setObjectName("ttcMaster");
        masterBtn_->setCheckable(true);
        masterBtn_->setToolTip("Start / stop the captions pipeline. While active, your selected mic streams to "
                               "tik.tools and the transcripts render in the overlay.");
        connect(primaryCta_, &QPushButton::clicked, this, [this]() {
            // Route the primary CTA off the trial state, not the raw JWT
            // cache - a stale JWT from a prior session used to send the
            // streamer to the topup page when they meant to sign in.
            const auto st = trial_ ? trial_->state() : TrialTracker::State::SignInRequired;
            if (st == TrialTracker::State::SignInRequired
                || st == TrialTracker::State::DuplicateAccount
                || Settings::instance().jwt().isEmpty()) {
                onLoginClicked();
            } else {
                onTopupClicked();
            }
        });
        connect(masterBtn_,  &QPushButton::clicked, this, &TikToolCaptionsDock::onMasterToggle);
        cta->addWidget(primaryCta_);
        cta->addWidget(masterBtn_);
        vv->addLayout(cta);

        v->addWidget(card);
    }

    // TikTok handle card - required to start captions.
    {
        auto [card, vv] = makeCard("TIKTOK @USERNAME");
        usernameEdit_ = new QLineEdit();
        usernameEdit_->setPlaceholderText("yourhandle (no @)");
        usernameEdit_->setMaxLength(40);
        usernameEdit_->setText(Settings::instance().tiktokUsername());
        connect(usernameEdit_, &QLineEdit::textChanged, this, &TikToolCaptionsDock::onUsernameChanged);
        vv->addWidget(usernameEdit_);
        usernameWarn_ = new QLabel("");
        usernameWarn_->setStyleSheet("color:#9ad;font-size:11px;");
        usernameWarn_->setWordWrap(true);
        vv->addWidget(usernameWarn_);
        v->addWidget(card);
    }

    // Audio card - primary mic + optional co-host audio
    {
        auto [card, vv] = makeCard("MIC / AUDIO");

        auto *micLabel = new QLabel("Your mic");
        micLabel->setObjectName("ttcRowLabel");
        vv->addWidget(micLabel);
        audioPicker_ = new QComboBox();
        connect(audioPicker_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onAudioPickerChanged);
        vv->addWidget(audioPicker_);

        auto *cohostLabel = new QLabel("Co-host audio (opponent voice in co-host / battle)");
        cohostLabel->setObjectName("ttcRowLabel");
        cohostLabel->setStyleSheet("margin-top:6px;");
        vv->addWidget(cohostLabel);
        cohostPicker_ = new QComboBox();
        connect(cohostPicker_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TikToolCaptionsDock::onCohostPickerChanged);
        vv->addWidget(cohostPicker_);

        auto *hint = new QLabel("Same audio device you set in TikTok LIVE Studio for opponent voice. Set to 'Off' if you stream solo.");
        hint->setObjectName("ttcHint");
        hint->setWordWrap(true);
        vv->addWidget(hint);

        rebuildAudioSources();
        v->addWidget(card);
    }

    // Language card
    {
        auto [card, vv] = makeCard("LANGUAGE");
        srcLang_ = new QComboBox();
        for (const auto &p : LANGS) srcLang_->addItem(p.second, p.first);
        toLang_  = new QComboBox();
        toLang_->addItem("No translation", "off");
        for (const auto &p : LANGS) {
            if (p.first == "auto") continue; // can't translate "to auto-detect"
            toLang_->addItem("to " + p.second, p.first);
        }

        auto cur = Settings::instance().language();
        int ix = srcLang_->findData(cur.sourceLanguage); if (ix >= 0) srcLang_->setCurrentIndex(ix);
        int tx = toLang_->findData(cur.translateTo);    if (tx >= 0) toLang_->setCurrentIndex(tx);

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

    // Style card - mirrors the captions-overlay editor controls on the website
    // so the streamer sees identical knobs in both surfaces.
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
        xpad_      = addSlider("X padding", 0, 40,  cur.xpad);

        // Font family - same Google Fonts allow-list the website uses, full
        // 64-entry list. Each row is painted in its own typeface so the
        // streamer sees the font in the dropdown without picking blind.
        font_ = new QComboBox();
        font_->setItemDelegate(new FontComboDelegate(font_));
        const char *fonts[] = {
            // Sans
            "Inter", "Roboto", "Open Sans", "Lato", "Montserrat", "Poppins",
            "Source Sans 3", "Nunito", "Work Sans", "Manrope", "Plus Jakarta Sans",
            "Outfit", "Sora", "DM Sans", "Quicksand", "Mulish", "Karla",
            "Public Sans", "Figtree", "Geist", "Onest",
            // Display + impact
            "Anton", "Bebas Neue", "Oswald", "Archivo Black", "Black Ops One",
            "Permanent Marker", "Press Start 2P", "Russo One", "Staatliches",
            "Squada One", "Teko", "Fjalla One", "Bowlby One", "Knewave",
            "Bungee", "Audiowide", "Faster One", "Monoton", "Righteous",
            "Saira Condensed",
            // Serif
            "Playfair Display", "Merriweather", "Lora", "Crimson Pro",
            "Source Serif 4", "EB Garamond", "Cormorant Garamond", "Cinzel",
            "Spectral",
            // Mono
            "JetBrains Mono", "Fira Code", "IBM Plex Mono", "Space Mono", "Roboto Mono",
            // Handwriting / script
            "Dancing Script", "Pacifico", "Caveat", "Shadows Into Light",
            "Indie Flower", "Satisfy", "Kalam", "Sacramento", "Great Vibes",
            // CJK fallback
            "Noto Sans KR", "Noto Sans JP", "Noto Sans SC", "Noto Serif KR",
        };
        for (auto *f : fonts) {
            // Item label is the font name; UserRole stores the family the
            // delegate uses to render the row text.
            font_->addItem(QString::fromLatin1(f), QString::fromLatin1(f));
            font_->setItemData(font_->count() - 1, QString::fromLatin1(f), Qt::UserRole);
        }
        int fix = font_->findData(cur.fontFamily); if (fix >= 0) font_->setCurrentIndex(fix);

        align_ = new QComboBox();
        align_->addItem("Left",   "left");
        align_->addItem("Center", "center");
        align_->addItem("Right",  "right");
        int aix = align_->findData(cur.align); if (aix >= 0) align_->setCurrentIndex(aix);

        fontColor_ = new QComboBox();
        const std::pair<QString, QString> palette[] = {
            {"White", "#FFFFFF"}, {"Black", "#000000"}, {"Yellow", "#FFD600"},
            {"Pink",  "#FF5BB3"}, {"Cyan",  "#00E0FF"}, {"Mint",  "#00FFA3"},
            {"Red",   "#FF4D4D"}, {"Orange","#FF9C2A"}, {"Lime",  "#B8FF3D"},
            {"Violet","#A55BFF"},
        };
        for (auto &p : palette) fontColor_->addItem(p.first, p.second);
        int cix = fontColor_->findData(cur.fontColor.toUpper()); if (cix >= 0) fontColor_->setCurrentIndex(cix);

        bgMode_ = new QComboBox();
        bgMode_->addItem("None",   "none");
        bgMode_->addItem("Shadow", "shadow");
        bgMode_->addItem("Box",    "box");
        int bix = bgMode_->findData(cur.bgMode); if (bix >= 0) bgMode_->setCurrentIndex(bix);

        shadow_         = new QCheckBox("Shadow");        shadow_->setChecked(cur.shadow);
        uppercase_      = new QCheckBox("UPPERCASE");     uppercase_->setChecked(cur.uppercase);
        showOriginal_   = new QCheckBox("Show original"); showOriginal_->setChecked(cur.showOriginal);
        showLangLabels_ = new QCheckBox("Lang labels");   showLangLabels_->setChecked(cur.showLangLabels);
        speakerNames_   = new QCheckBox("Speaker names"); speakerNames_->setChecked(cur.speakerNames);
        speakerColors_  = new QCheckBox("Speaker colors");speakerColors_->setChecked(cur.speakerColors);

        auto makePro = []() {
            auto *p = new QLabel("PRO");
            p->setStyleSheet("background:rgba(255,213,107,0.16);color:#ffd86b;border-radius:8px;padding:2px 7px;font-size:9px;font-weight:800;letter-spacing:0.1em;");
            return p;
        };
        auto *row0 = new QHBoxLayout();
        row0->addWidget(new QLabel("Font"));
        row0->addWidget(font_);
        row0->addWidget(makePro());
        vv->addLayout(row0);

        auto *row1 = new QHBoxLayout();
        row1->addWidget(new QLabel("Align"));
        row1->addWidget(align_);
        row1->addWidget(makePro());
        row1->addWidget(new QLabel("Color"));
        row1->addWidget(fontColor_);
        vv->addLayout(row1);

        auto *row1b = new QHBoxLayout();
        row1b->addWidget(new QLabel("Background"));
        row1b->addWidget(bgMode_);
        row1b->addStretch();
        vv->addLayout(row1b);

        auto *row2 = new QHBoxLayout();
        row2->addWidget(shadow_);
        row2->addWidget(uppercase_);
        row2->addStretch();
        vv->addLayout(row2);

        auto *row3 = new QHBoxLayout();
        row3->addWidget(showOriginal_);
        row3->addWidget(makePro());
        row3->addSpacing(8);
        row3->addWidget(showLangLabels_);
        row3->addWidget(makePro());
        row3->addStretch();
        vv->addLayout(row3);

        auto *row4 = new QHBoxLayout();
        row4->addWidget(speakerNames_);
        row4->addWidget(makePro());
        row4->addSpacing(8);
        row4->addWidget(speakerColors_);
        row4->addWidget(makePro());
        row4->addStretch();
        vv->addLayout(row4);

        for (auto *sl : {fontSize_, bgOpacity_, position_, maxLines_, xpad_})
            connect(sl, &QSlider::valueChanged, this, &TikToolCaptionsDock::onStyleChanged);
        for (auto *cb : {font_, align_, fontColor_, bgMode_})
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &TikToolCaptionsDock::onStyleChanged);
        for (auto *chk : {shadow_, uppercase_, showOriginal_, showLangLabels_, speakerNames_, speakerColors_})
            connect(chk, &QCheckBox::toggled, this, &TikToolCaptionsDock::onStyleChanged);

        // Watermark toggle - premium-only. Free trial keeps it forced ON.
        auto *wmRow = new QHBoxLayout();
        watermarkToggle_ = new QCheckBox("Show tik.tools watermark");
        watermarkToggle_->setChecked(true);
        connect(watermarkToggle_, &QCheckBox::toggled, this, &TikToolCaptionsDock::onWatermarkToggled);
        auto *wmLock = new QLabel("PRO");
        wmLock->setObjectName("ttcLockChip");
        wmLock->setStyleSheet("background:rgba(255,213,107,0.16);color:#ffd86b;border-radius:8px;padding:2px 8px;font-size:9px;font-weight:800;letter-spacing:0.1em;");
        wmRow->addWidget(watermarkToggle_);
        wmRow->addWidget(wmLock);
        wmRow->addStretch();
        vv->addLayout(wmRow);

        v->addWidget(card);
    }

    // Live transcript preview intentionally omitted - the OBS canvas already
    // shows the captions overlay so duplicating them inside the dock was
    // redundant + chewed vertical space on smaller monitors.
    transcript_ = nullptr;

    // Footer
    {
        // "One-click OBS setup" is the kingmaker button: vertical canvas +
        // webcam in scene + virtual camera running, all in one tap. Lives
        // above the install/wizard row so it is the FIRST thing the streamer
        // sees if they bypassed the wizard.
        auto *oneClick = new QPushButton("One-click OBS setup (vertical + camera + virtual cam)");
        oneClick->setObjectName("ttcPrimary");
        connect(oneClick, &QPushButton::clicked, this, &TikToolCaptionsDock::onAutoConfigureClicked);
        v->addWidget(oneClick);

        auto *footer = new QHBoxLayout();
        auto *install = new QPushButton("Install captions overlay");
        install->setObjectName("ttcFooterBtn");
        auto *wizard = new QPushButton("Re-run wizard");
        wizard->setObjectName("ttcFooterBtn");
        connect(install, &QPushButton::clicked, this, &TikToolCaptionsDock::onInstallOverlay);
        connect(wizard,  &QPushButton::clicked, this, &TikToolCaptionsDock::onWizardClicked);
        footer->addWidget(install);
        footer->addWidget(wizard);
        v->addLayout(footer);

        auto *footer2 = new QHBoxLayout();
        auto *signout = new QPushButton("Sign out");
        signout->setObjectName("ttcFooterBtn");
        auto *reset   = new QPushButton("Reset all settings");
        reset->setObjectName("ttcFooterBtnDanger");
        connect(signout, &QPushButton::clicked, this, &TikToolCaptionsDock::onSignOutClicked);
        connect(reset,   &QPushButton::clicked, this, &TikToolCaptionsDock::onResetAllClicked);
        footer2->addWidget(signout);
        footer2->addWidget(reset);
        v->addLayout(footer2);
    }

    v->addStretch();
}

void TikToolCaptionsDock::applyStyle() {
    QFile f(":/tiktok/style.qss");
    if (f.open(QIODevice::ReadOnly)) {
        setStyleSheet(QString::fromUtf8(f.readAll()));
    } else {
        // Fallback inline QSS keeps the dock usable even if the resource bundle
        // failed to ship. Glassmorphism is approximated since Qt has no real
        // backdrop-filter; we lean on translucent surfaces + thin borders.
        setStyleSheet(R"(
            #TikToolCaptionsDockRoot { background: transparent; }
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
            #ttcFooterBtnDanger { background: rgba(255,90,90,0.08); color: #ff9b9b;
                border: 1px solid rgba(255,90,90,0.25); border-radius: 10px;
                padding: 8px 12px; }
            #ttcFooterBtnDanger:hover { background: rgba(255,90,90,0.18); }
            #ttcZoomBtn { background: rgba(255,255,255,0.06); color: #fff;
                border: 1px solid rgba(255,255,255,0.10); border-radius: 6px;
                padding: 2px 4px; font-size: 10px; font-weight: 700; }
            #ttcZoomBtn:hover { background: rgba(255,255,255,0.12); }
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
    auto fillPicker = [](QComboBox *picker, bool withOff, const QString &savedMode,
                         const QString &savedDevice, const QString &savedSource) {
        picker->blockSignals(true);
        picker->clear();
        if (withOff) picker->addItem(QStringLiteral("Off (solo stream)"),
                                     QVariant::fromValue(QStringList{ "off", QString(), QString() }));
        const auto devices = AudioCapture::enumerateSystemDevices();
        if (!devices.isEmpty()) {
            picker->addItem(QStringLiteral("--- System audio devices ---"));
            auto *model = qobject_cast<QStandardItemModel *>(picker->model());
            if (model) {
                QStandardItem *hdr = model->item(model->rowCount() - 1);
                if (hdr) hdr->setFlags(hdr->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
            }
            for (const auto &d : devices)
                picker->addItem(d.displayName, QVariant::fromValue(QStringList{ "system", d.id, d.displayName }));
        }
        const auto sources = AudioCapture::enumerateAudioSources();
        if (!sources.isEmpty()) {
            picker->addItem(QStringLiteral("--- OBS audio sources ---"));
            auto *model = qobject_cast<QStandardItemModel *>(picker->model());
            if (model) {
                QStandardItem *hdr = model->item(model->rowCount() - 1);
                if (hdr) hdr->setFlags(hdr->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
            }
            for (const auto &name : sources)
                picker->addItem(name, QVariant::fromValue(QStringList{ "obs-source", QString(), name }));
        }
        // Restore selection
        for (int i = 0; i < picker->count(); ++i) {
            QStringList v = picker->itemData(i).toStringList();
            if (v.size() < 3) continue;
            if (savedMode == "off" && v[0] == "off") { picker->setCurrentIndex(i); break; }
            if (savedMode == "system" && v[0] == "system" && v[1] == savedDevice) { picker->setCurrentIndex(i); break; }
            if (savedMode == "obs-source" && v[0] == "obs-source" && v[2] == savedSource) { picker->setCurrentIndex(i); break; }
        }
        picker->blockSignals(false);
    };
    fillPicker(audioPicker_, /*withOff=*/false,
        Settings::instance().audioMode(),
        Settings::instance().audioDeviceId(),
        Settings::instance().audioSourceName());
    if (cohostPicker_) {
        fillPicker(cohostPicker_, /*withOff=*/true,
            Settings::instance().cohostMode(),
            Settings::instance().cohostDeviceId(),
            Settings::instance().cohostSourceName());
    }
    return;
    // Legacy path below kept dead so the diff stays minimal - new code above
    // handles both pickers via fillPicker.
    audioPicker_->blockSignals(true);
    audioPicker_->clear();

    // Group 1: system mic / line-in / loopback devices (most streamers want
    // this even if they have not added a Mic/Aux source in OBS yet).
    const auto devices = AudioCapture::enumerateSystemDevices();
    if (!devices.isEmpty()) {
        audioPicker_->addItem(QStringLiteral("--- System audio devices ---"));
        // Mark the section header so the change handler ignores it on click.
        auto *model = qobject_cast<QStandardItemModel *>(audioPicker_->model());
        if (model) {
            QStandardItem *hdr = model->item(model->rowCount() - 1);
            if (hdr) { hdr->setFlags(hdr->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled)); }
        }
        for (const auto &d : devices) {
            const QString label = d.displayName;
            audioPicker_->addItem(label, QVariant::fromValue(QStringList{ "system", d.id, d.displayName }));
        }
    }

    // Group 2: existing OBS audio sources the streamer already added.
    const auto sources = AudioCapture::enumerateAudioSources();
    if (!sources.isEmpty()) {
        audioPicker_->addItem(QStringLiteral("--- OBS audio sources ---"));
        auto *model = qobject_cast<QStandardItemModel *>(audioPicker_->model());
        if (model) {
            QStandardItem *hdr = model->item(model->rowCount() - 1);
            if (hdr) { hdr->setFlags(hdr->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled)); }
        }
        for (const auto &name : sources) {
            audioPicker_->addItem(name, QVariant::fromValue(QStringList{ "obs-source", QString(), name }));
        }
    }

    if (audioPicker_->count() == 0)
        audioPicker_->addItem(QStringLiteral("<no audio devices found>"));

    // Restore previous selection if possible: prefer the saved system device id.
    const QString savedMode   = Settings::instance().audioMode();
    const QString savedDevice = Settings::instance().audioDeviceId();
    const QString savedSource = Settings::instance().audioSourceName();
    for (int i = 0; i < audioPicker_->count(); ++i) {
        QStringList v = audioPicker_->itemData(i).toStringList();
        if (v.size() < 3) continue;
        if (savedMode == "system" && v[0] == "system" && v[1] == savedDevice) {
            audioPicker_->setCurrentIndex(i); break;
        }
        if (savedMode == "obs-source" && v[0] == "obs-source" && v[2] == savedSource) {
            audioPicker_->setCurrentIndex(i); break;
        }
    }
    audioPicker_->blockSignals(false);
}

void TikToolCaptionsDock::onAudioPickerChanged(int) {
    if (!audioPicker_) return;
    QStringList v = audioPicker_->currentData().toStringList();
    if (v.size() < 3) return; // header / placeholder rows carry no payload
    const QString &mode    = v[0];
    const QString &deviceId= v[1];
    const QString &name    = v[2];
    Settings::instance().setAudioMode(mode);
    Settings::instance().setAudioSourceName(name);
    Settings::instance().setAudioDeviceId(deviceId);
    Settings::instance().save();
    if (!audio_) return;
    if (mode == "system")      audio_->bindToSystemDevice(deviceId, name);
    else                       audio_->bindToSource(name);
}

void TikToolCaptionsDock::onCohostPickerChanged(int) {
    if (!cohostPicker_) return;
    QStringList v = cohostPicker_->currentData().toStringList();
    if (v.size() < 3) return;
    const QString &mode    = v[0];
    const QString &deviceId= v[1];
    const QString &name    = v[2];
    Settings::instance().setCohostMode(mode);
    Settings::instance().setCohostSourceName(name);
    Settings::instance().setCohostDeviceId(deviceId);
    Settings::instance().save();
    if (!cohostAudio_) return;
    if (mode == "off") {
        cohostAudio_->setStreaming(false);
        cohostAudio_->unbind();
        std::lock_guard<std::mutex> g(cohostMutex_);
        cohostFrame_.clear();
        cohostFrameTs_ = 0;
        return;
    }
    if (mode == "system") cohostAudio_->bindToSystemDevice(deviceId, name);
    else                  cohostAudio_->bindToSource(name);
    if (streaming_) cohostAudio_->setStreaming(true);
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
    s.xpad       = xpad_ ? xpad_->value() : s.xpad;
    s.align      = align_->currentData().toString();
    s.fontColor  = fontColor_->currentData().toString();
    s.fontFamily = font_   ? font_->currentData().toString()   : s.fontFamily;
    s.bgMode     = bgMode_ ? bgMode_->currentData().toString() : s.bgMode;
    s.shadow         = shadow_->isChecked();
    s.uppercase      = uppercase_->isChecked();
    s.showOriginal   = showOriginal_   ? showOriginal_->isChecked()   : s.showOriginal;
    s.showLangLabels = showLangLabels_ ? showLangLabels_->isChecked() : s.showLangLabels;
    s.speakerNames   = speakerNames_   ? speakerNames_->isChecked()   : s.speakerNames;
    s.speakerColors  = speakerColors_  ? speakerColors_->isChecked()  : s.speakerColors;
    Settings::instance().setStyle(s);
    saveDebounce_->start();
}

void TikToolCaptionsDock::onMasterToggle() {
    const bool wantStart = masterBtn_->isChecked();
    // Gating only applies when STARTING. Stop must always stop, no matter
    // what state the trial is in - otherwise hitting Stop on an expired
    // trial used to open the topup web page instead of tearing down audio.
    if (wantStart) {
        // Username gate. Read from BOTH the lineEdit and settings - a
        // streamer who typed their handle into the dock field but hit Start
        // before the textChanged save was committed used to get bumped into
        // the wizard. Now we lazily persist whichever surface has a valid
        // handle and proceed.
        QString uname = Settings::instance().tiktokUsername();
        if (uname.isEmpty() && usernameEdit_) {
            QString fromEdit = usernameEdit_->text().trimmed();
            if (fromEdit.startsWith('@')) fromEdit = fromEdit.mid(1);
            fromEdit = fromEdit.toLower();
            static const QRegularExpression re("^[a-zA-Z0-9._]{2,40}$");
            if (re.match(fromEdit).hasMatch()) {
                Settings::instance().setTiktokUsername(fromEdit);
                Settings::instance().save();
                uname = fromEdit;
            }
        }
        if (uname.isEmpty()) {
            masterBtn_->setChecked(false);
            statusPill_->setText("Add your TikTok @username first");
            // Just focus the field instead of relaunching the whole wizard -
            // the streamer already finished it once and the relaunch was the
            // most-confusing symptom of this bug.
            if (usernameEdit_) {
                usernameEdit_->setFocus();
                usernameEdit_->selectAll();
            } else {
                onWizardClicked();
            }
            return;
        }
        if (Settings::instance().jwt().isEmpty()) {
            masterBtn_->setChecked(false);
            statusPill_->setText("Sign in to start captions");
            onLoginClicked();
            return;
        }
        if (trial_ && trial_->state() == TrialTracker::State::DuplicateAccount) {
            masterBtn_->setChecked(false);
            statusPill_->setText("Use original account: " + trial_->duplicateOriginalEmail());
            onLoginClicked();
            return;
        }
        if (trial_ && trial_->minutesLeft() <= 0
            && trial_->state() != TrialTracker::State::Paid) {
            masterBtn_->setChecked(false);
            statusPill_->setText("Free trial used up");
            onTopupClicked();
            return;
        }
    }
    streaming_ = masterBtn_->isChecked();
    masterBtn_->setText(streaming_ ? "Stop captions" : "Start captions");
    // Remember the streamer's intent so we auto-resume on the next OBS open.
    Settings::instance().setStreamingPersisted(streaming_);
    Settings::instance().save();
    if (streaming_) {
        if (audio_->boundSourceName().isEmpty()) {
            const QString mode = Settings::instance().audioMode();
            const QString did  = Settings::instance().audioDeviceId();
            const QString src  = Settings::instance().audioSourceName();
            if (mode == "system" && !did.isEmpty()) audio_->bindToSystemDevice(did, src);
            else if (!src.isEmpty())                audio_->bindToSource(src);
            else if (!audio_->bindToSource("")) {
                statusPill_->setText("Pick an audio device first");
                masterBtn_->setChecked(false);
                streaming_ = false;
                masterBtn_->setText("Start captions");
                return;
            }
        }
        audio_->setStreaming(true);
        // Co-host capture: bind + stream if the streamer picked a device.
        if (cohostAudio_) {
            const QString cmode = Settings::instance().cohostMode();
            if (cmode != "off") {
                if (cohostAudio_->boundSourceName().isEmpty()) {
                    if (cmode == "system") cohostAudio_->bindToSystemDevice(Settings::instance().cohostDeviceId(), Settings::instance().cohostSourceName());
                    else                   cohostAudio_->bindToSource(Settings::instance().cohostSourceName());
                }
                cohostAudio_->setStreaming(true);
            }
        }
        auto lc = Settings::instance().language();
        auto sc = Settings::instance().session();
        const QString mode = (lc.translateTo != "off") ? "translate" : sc.mode;
        // We pass the apiKey here, not the JWT. captions.ts authenticates the
        // WS via x-api-key / apiKey= query param and meters billing per-key.
        ws_->connectWith(Settings::instance().apiKey(),
                         Settings::instance().deviceId(),
                         Settings::instance().tiktokUsername(),
                         mode, lc.sourceLanguage, lc.translateTo);
    } else {
        audio_->setStreaming(false);
        if (cohostAudio_) cohostAudio_->setStreaming(false);
        ws_->disconnectWs();
    }
}

void TikToolCaptionsDock::onInstallOverlay() {
    // Drop the mint-endpoint round-trip. /captions/overlay/mint was never
    // implemented on api-tik-tools so this call was 404-ing silently and the
    // install never ran. We pass the streamer's JWT directly - the overlay
    // worker already accepts jwtKey on the URL and the wizard takes the
    // same path with no problem.
    if (Settings::instance().jwt().isEmpty()) {
        statusPill_->setText("Sign in first - cannot install overlay");
        onLoginClicked();
        return;
    }
    const bool wm = trial_ ? trial_->watermark() : true;
    const QString url = installer_->installOrUpdate(
        Settings::instance().jwt(),
        /*setupId=*/QString(),
        Settings::instance().style(),
        Settings::instance().language(),
        wm);
    if (url.isEmpty()) {
        statusPill_->setText("Overlay install failed - check obs-browser is enabled");
    } else {
        statusPill_->setText("Overlay installed + locked to scene");
    }
}

void TikToolCaptionsDock::onLoginClicked() {
    // The captions plugin-link page lives at /captions/plugin-link?device=<id>
    // and writes the {jwt, apiKey, email} handoff into TikTok cloud under
    // `captions:plugin-link:<deviceId>` (TTL 5 min, single-read). We embed the
    // device id directly INSIDE the returnTo URL so login redirects preserve
    // it through Google OAuth + the post-login navigateTo step.
    // Embed fingerprint in the returnTo so the plugin-link page can pass it
    // to /api/captions/plugin-link, which writes it to TikTok cloud as part of
    // the duplicate-account detection ledger.
    const QString returnUrl = QString::fromLatin1(TIKTOOL_WEB_BASE)
        + "/captions/plugin-link?device=" + Settings::instance().deviceId()
        + "&fp=" + Settings::instance().fingerprint();
    const QString url = QString::fromLatin1(TIKTOOL_WEB_BASE)
        + "/login?from=obs-plugin&returnTo=" + QUrl::toPercentEncoding(returnUrl);
    QDesktopServices::openUrl(QUrl(url));
    statusPill_->setText("Finish sign-in in your browser...");

    // Poll the trial endpoint aggressively for 5 min. The server returns the
    // {jwt, apiKey, email} fields directly the first time we GET after the
    // handoff lands; we save them, stop polling, and the trial tracker flips
    // out of SignInRequired automatically.
    auto *poll = new QTimer(this);
    poll->setInterval(3000);
    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    deadline->setInterval(5 * 60 * 1000);
    QObject::connect(deadline, &QTimer::timeout, this, [this, poll, deadline]() {
        poll->stop();
        poll->deleteLater();
        deadline->deleteLater();
        if (statusPill_ && Settings::instance().jwt().isEmpty())
            statusPill_->setText("Sign-in timed out - click Sign in again");
    });
    QObject::connect(poll, &QTimer::timeout, this, [this, poll, deadline]() {
        api_->checkTrial(Settings::instance().deviceId(), [this, poll, deadline]
            (bool ok, const QJsonObject &b, int) {
            if (!ok) return;
            const QString jwt = b.value("jwt").toString();
            if (jwt.isEmpty()) return;
            Settings::instance().setJwt(jwt);
            api_->setJwt(jwt);
            if (b.contains("apiKey")) {
                Settings::instance().setApiKey(b.value("apiKey").toString());
                api_->setApiKey(b.value("apiKey").toString());
            }
            if (b.contains("email"))
                Settings::instance().setAccountEmail(b.value("email").toString());
            Settings::instance().save();
            poll->stop();
            poll->deleteLater();
            deadline->stop();
            deadline->deleteLater();
            // Force a refresh so the trial state flips to AccountTrial + the
            // minutes counter populates.
            if (trial_) trial_->refresh();
            if (statusPill_)
                statusPill_->setText("Signed in - ready to start captions");
        });
    });
    poll->start();
    deadline->start();
}

void TikToolCaptionsDock::onTopupClicked() {
    // Drop the streamer straight into the plans grid on the captions page.
    // The /captions#plans anchor scrolls past the hero + lands on the
    // Casual / Pro / Extreme weekly + monthly billing Checkout buttons.
    QString url = QString::fromLatin1(TIKTOOL_WEB_BASE) + "/captions?from=obs-plugin#plans";
    if (!Settings::instance().jwt().isEmpty())
        url = QString::fromLatin1(TIKTOOL_WEB_BASE) + "/captions?from=obs-plugin&device="
            + Settings::instance().deviceId() + "#plans";
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

void TikToolCaptionsDock::applyUiScale() {
    const double s = Settings::instance().uiScale();
    QFont f = font();
    // Default Qt UI font is 9pt on Windows. Scale by uiScale_, clamped to a
    // legible band. We change the dock's base font + children inherit.
    int base = 9;
    int pt = qBound(7, int(std::round(base * s)), 16);
    f.setPointSize(pt);
    setFont(f);
    // Minimum width grows with scale so the cards do not get squished into a
    // tiny column on a 1.4x setting.
    setMinimumWidth(int(360 * s));
}

void TikToolCaptionsDock::onUiScaleSmaller() {
    const double cur = Settings::instance().uiScale();
    const double next = std::max(0.7, cur - 0.15);
    Settings::instance().setUiScale(next);
    Settings::instance().save();
    applyUiScale();
}

void TikToolCaptionsDock::onUiScaleLarger() {
    const double cur = Settings::instance().uiScale();
    const double next = std::min(1.6, cur + 0.15);
    Settings::instance().setUiScale(next);
    Settings::instance().save();
    applyUiScale();
}

void TikToolCaptionsDock::onUsernameChanged() {
    if (!usernameEdit_) return;
    QString u = usernameEdit_->text().trimmed();
    if (u.startsWith('@')) u = u.mid(1);
    u = u.toLower();
    static const QRegularExpression re("^[a-zA-Z0-9._]{2,40}$");
    const bool ok = re.match(u).hasMatch();
    if (ok) {
        Settings::instance().setTiktokUsername(u);
        Settings::instance().save();
        if (usernameWarn_) { usernameWarn_->setText("Saved as @" + u); usernameWarn_->setStyleSheet("color:#7ce17c;font-size:11px;"); }
    } else {
        if (usernameWarn_) { usernameWarn_->setText(u.isEmpty() ? "Required to start captions" : "Invalid - 2-40 chars: letters, digits, dot, underscore"); usernameWarn_->setStyleSheet("color:#ff7b7b;font-size:11px;"); }
    }
}

void TikToolCaptionsDock::onWatermarkToggled(bool on) {
    // Watermark / premium gating is server-driven via the trial flag. The
    // dock toggle is purely a UX preview: when the streamer is paid, we
    // expose a "Remove tik.tools badge" checkbox the overlay reads via
    // `wm=` URL param. When the streamer is free, the toggle is disabled +
    // forced on so they cannot pretend to be paid client-side. Server
    // re-checks on every WS open anyway.
    Q_UNUSED(on);
    Settings::instance().save();
    saveDebounce_->start();
}

void TikToolCaptionsDock::onSignOutClicked() {
    if (QMessageBox::question(this, "Sign out",
        "Sign out of tik.tools on this OBS install? "
        "Your local style + mic choice are kept; only the JWT + API key are cleared.",
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    if (streaming_) {
        streaming_ = false;
        if (audio_) audio_->setStreaming(false);
        if (ws_)    ws_->disconnectWs();
        if (masterBtn_) { masterBtn_->setChecked(false); masterBtn_->setText("Start captions"); }
    }
    Settings::instance().setJwt("");
    Settings::instance().setApiKey("");
    Settings::instance().setAccountEmail("");
    Settings::instance().save();
    if (api_) { api_->setJwt(""); api_->setApiKey(""); }
    if (trial_) trial_->refresh(); // clears state to SignInRequired immediately
    // Force the dock onto the SignInRequired pill right away so we do not
    // render stale "Free trial ended" between sign-out and the next 3 s tick.
    if (statusPill_) {
        statusPill_->setText("Sign in to start your 60 free minutes");
        statusPill_->setProperty("variant", "signin");
        statusPill_->style()->unpolish(statusPill_);
        statusPill_->style()->polish(statusPill_);
    }
    if (primaryCta_) primaryCta_->setText("Sign in with tik.tools");
    if (minutesLabel_) minutesLabel_->setText("- min left");
}

void TikToolCaptionsDock::onAutoConfigureClicked() {
    SceneSetup s;
    auto r = s.runAll();
    QStringList lines;
    lines << (r.canvasOk     ? "Canvas 1080x1920 OK"     : "Canvas FAILED");
    lines << (r.cameraOk     ? "Camera OK"               : "Camera FAILED");
    lines << (r.virtualCamOk ? "Virtual Camera ON"       : "Virtual Camera FAILED");
    if (statusPill_) {
        statusPill_->setText(lines.join(" / "));
    }
    // Also install the overlay so a single button truly leaves them stream-ready.
    if (!Settings::instance().jwt().isEmpty()) {
        const bool wm = trial_ ? trial_->watermark() : true;
        installer_->installOrUpdate(
            Settings::instance().jwt(), QString(),
            Settings::instance().style(), Settings::instance().language(), wm);
    }
}

void TikToolCaptionsDock::onResetAllClicked() {
    if (QMessageBox::question(this, "Reset all settings",
        "Wipe every TikTok Captions - by TikTools setting on this machine?\n\n"
        "This clears your sign-in, your audio choice, your style sliders, your language pick, "
        "and re-arms the first-run wizard. Your device identifier and hardware fingerprint stay "
        "so the anti-abuse layer still recognises this machine.",
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    if (streaming_) {
        streaming_ = false;
        if (audio_) audio_->setStreaming(false);
        if (ws_)    ws_->disconnectWs();
    }
    if (audio_) audio_->unbind();
    Settings::instance().resetAll();
    if (api_) {
        api_->setJwt("");
        api_->setApiKey("");
        api_->setFingerprint(Settings::instance().fingerprint());
    }
    rebuildAudioSources();
    if (trial_) trial_->refresh();
    if (statusPill_) statusPill_->setText("Settings reset - opening wizard");
    if (minutesLabel_) minutesLabel_->setText("- min left");
    if (masterBtn_) { masterBtn_->setChecked(false); masterBtn_->setText("Start captions"); }
    QTimer::singleShot(300, this, &TikToolCaptionsDock::onWizardClicked);
}

void TikToolCaptionsDock::onTrialChanged() {
    if (!trial_) return;
    const auto state = trial_->state();
    const int mins   = trial_->minutesLeft();
    minutesLabel_->setText(QString("%1 min left").arg(mins));
    // Premium gating: paid users can pick fancy fonts, alignment, speaker
    // names, speaker colors, AND turn the watermark off. Free trial users
    // get the core controls but the premium ones are disabled with a clear
    // visual cue (grayed + tooltip explaining the upgrade path).
    const bool paid = (state == TrialTracker::State::Paid);
    auto setPremium = [paid](QWidget *w, const QString &tip) {
        if (!w) return;
        w->setEnabled(paid);
        w->setToolTip(paid ? QString() : tip);
        w->setStyleSheet(paid ? QString() : "color:#777;");
    };
    const QString PREMIUM_TIP = "Premium - pick a captions plan at tik.tools/captions to unlock.";
    setPremium(font_, PREMIUM_TIP);
    setPremium(align_, PREMIUM_TIP);
    setPremium(speakerNames_, PREMIUM_TIP);
    setPremium(speakerColors_, PREMIUM_TIP);
    setPremium(showOriginal_, PREMIUM_TIP);
    setPremium(showLangLabels_, PREMIUM_TIP);
    if (watermarkToggle_) {
        watermarkToggle_->setEnabled(paid);
        watermarkToggle_->setToolTip(paid ? QString() : PREMIUM_TIP);
        if (!paid) {
            // Force watermark on for free users.
            watermarkToggle_->blockSignals(true);
            watermarkToggle_->setChecked(true);
            watermarkToggle_->blockSignals(false);
        }
    }
    switch (state) {
        case TrialTracker::State::Paid:
            statusPill_->setText("Paid plan");
            statusPill_->setProperty("variant", "paid");
            primaryCta_->setText("TOP-UP");
            break;
        case TrialTracker::State::AccountTrial:
            statusPill_->setText(QString("Free trial - %1 min left").arg(mins));
            statusPill_->setProperty("variant", "trial");
            primaryCta_->setText("TOP-UP");
            break;
        case TrialTracker::State::AccountExpired:
            statusPill_->setText("Free trial ended");
            statusPill_->setProperty("variant", "expired");
            primaryCta_->setText("TOP-UP");
            // If user was streaming when minutes ran out, stop locally so the
            // overlay does not sit on stale audio frames being silently
            // dropped server-side.
            if (streaming_) {
                streaming_ = false;
                if (audio_) audio_->setStreaming(false);
                if (ws_)    ws_->disconnectWs();
                if (masterBtn_) {
                    masterBtn_->setChecked(false);
                    masterBtn_->setText("Start captions");
                }
            }
            break;
        case TrialTracker::State::SignInRequired:
            statusPill_->setText("Sign in to start your 60 free minutes");
            statusPill_->setProperty("variant", "signin");
            primaryCta_->setText("Sign in with tik.tools");
            break;
        case TrialTracker::State::DuplicateAccount: {
            const QString origin = trial_->duplicateOriginalEmail();
            statusPill_->setText(origin.isEmpty()
                ? "Trial already used on this machine"
                : "Use " + origin + " - account already linked here");
            statusPill_->setProperty("variant", "expired");
            primaryCta_->setText("Sign in with original account");
            // Force-stop any in-flight streaming.
            if (streaming_) {
                streaming_ = false;
                if (audio_) audio_->setStreaming(false);
                if (ws_)    ws_->disconnectWs();
                if (masterBtn_) { masterBtn_->setChecked(false); masterBtn_->setText("Start captions"); }
            }
            break;
        }
        default:
            statusPill_->setText("Connecting...");
            statusPill_->setProperty("variant", "neutral");
            break;
    }
    // Re-evaluate stylesheet so the variant property drives the pill color.
    statusPill_->style()->unpolish(statusPill_);
    statusPill_->style()->polish(statusPill_);
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
        ws_->connectWith(Settings::instance().apiKey(),
                         Settings::instance().deviceId(),
                         Settings::instance().tiktokUsername(),
                         mode, lc.sourceLanguage, lc.translateTo);
    }
}

} // namespace tiktok
