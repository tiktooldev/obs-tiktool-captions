#include "captions-wizard.hpp"
#include "audio-capture.hpp"
#include "settings.hpp"
#include "overlay-installer.hpp"
#include "version.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizardPage>

namespace tiktool {

CaptionsWizard::CaptionsWizard(QWidget *parent) : QWizard(parent) {
    setWindowTitle("TikTool Captions - Setup");
    setMinimumSize(560, 600);
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::IgnoreSubTitles, false);

    buildWelcomePage();
    buildAudioPage();
    buildLanguagePage();
    buildDemoPage();
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
        "<b>Your first 10 minutes are on us.</b> Translation burns 2 min per minute "
        "(1 for source + 1 for translation). After that, sign in for another 10 free min, "
        "then top up minutes any time.<br><br>"
        "We listen to whatever audio you already route through OBS, send it straight to "
        "tik.tools' caption engine, and overlay the result on your scene at 9:16."
        "</p>");
    body->setWordWrap(true);
    v->addWidget(body);
    addPage(makePage("Welcome to TikTool Live Captions",
                     "Three minutes to set up. Streamer-grade output.", w));
}

void CaptionsWizard::buildAudioPage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    auto *info = new QLabel("Pick the OBS audio source we should listen to. This is usually "
                            "your microphone, or a mix that includes your voice.");
    info->setWordWrap(true);
    v->addWidget(info);

    auto *combo = new QComboBox();
    for (const auto &s : AudioCapture::enumerateAudioSources())
        combo->addItem(s);
    const QString prev = Settings::instance().audioSourceName();
    if (!prev.isEmpty()) {
        int ix = combo->findText(prev);
        if (ix >= 0) combo->setCurrentIndex(ix);
    }
    QObject::connect(combo, &QComboBox::currentTextChanged, this, [](const QString &name) {
        Settings::instance().setAudioSourceName(name);
    });
    v->addWidget(combo);

    auto *hint = new QLabel("Don't see your mic? Add it as a Mic/Aux source in OBS first, "
                            "then click Back and Next again.");
    hint->setStyleSheet("color:#888;font-size:11px");
    hint->setWordWrap(true);
    v->addWidget(hint);

    addPage(makePage("Choose your mic",
                     "We pick up the audio you already route through OBS - no extra driver needed.", w));
}

void CaptionsWizard::buildLanguagePage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);

    auto *src = new QComboBox();
    const QStringList srcCodes  = {"auto","en","es","pt","fr","de","it","tr","ru","ar","ja","ko","zh"};
    const QStringList srcNames  = {"Auto-detect","English","Spanish","Portuguese","French","German",
                                   "Italian","Turkish","Russian","Arabic","Japanese","Korean","Chinese"};
    for (int i = 0; i < srcCodes.size(); ++i) src->addItem(srcNames[i], srcCodes[i]);

    auto *trg = new QComboBox();
    const QStringList tgtCodes  = {"off","en","es","pt","fr","de","it","tr","ru","ar","ja","ko","zh"};
    const QStringList tgtNames  = {"No translation","to English","to Spanish","to Portuguese",
                                   "to French","to German","to Italian","to Turkish","to Russian",
                                   "to Arabic","to Japanese","to Korean","to Chinese"};
    for (int i = 0; i < tgtCodes.size(); ++i) trg->addItem(tgtNames[i], tgtCodes[i]);

    auto cur = Settings::instance().language();
    int ix = srcCodes.indexOf(cur.sourceLanguage); if (ix >= 0) src->setCurrentIndex(ix);
    int tx = tgtCodes.indexOf(cur.translateTo);    if (tx >= 0) trg->setCurrentIndex(tx);

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
        "<b>'TikTool Captions'</b> into your current scene and start a 30-second "
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
            Settings::instance().language());
        btn->setText(url.isEmpty() ? "Install failed - see status" : "Installed");
        btn->setDisabled(true);
    });
    v->addWidget(btn);
    addPage(makePage("Drop the overlay in your scene",
                     "Vertical 9:16 by default. Move it anywhere on your canvas.", w));
}

void CaptionsWizard::buildDonePage() {
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    auto *body = new QLabel(
        "<p style='font-size:14px;line-height:1.6'>You're set.<br><br>"
        "Hit <b>Start captions</b> in the dock to begin. The first 10 minutes are free; "
        "sign in to claim 10 more, then top up directly from the dock whenever you need more."
        "</p>");
    body->setWordWrap(true);
    v->addWidget(body);

    auto *signIn = new QPushButton("Sign in now (recommended)");
    QObject::connect(signIn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(
            QString::fromLatin1(TIKTOOL_WEB_BASE)
            + "/login?from=obs-plugin&device=" + Settings::instance().deviceId()));
    });
    v->addWidget(signIn);

    addPage(makePage("All set",
                     "You can re-run this wizard from the dock at any time.", w));
}

} // namespace tiktool
