#include "settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace tiktool {

Settings &Settings::instance() {
    static Settings s;
    return s;
}

Settings::Settings() = default;

QString Settings::configFilePath() const {
    char *path = obs_module_config_path("settings.json");
    QString out = QString::fromUtf8(path ? path : "");
    bfree(path);
    if (out.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(base + "/obs-tiktool-captions");
        out = base + "/obs-tiktool-captions/settings.json";
    } else {
        QFileInfo fi(out);
        QDir().mkpath(fi.absolutePath());
    }
    return out;
}

void Settings::load() {
    std::lock_guard<std::mutex> g(m_);
    QFile f(configFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        if (deviceId_.isEmpty())
            deviceId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) fromJson(doc.object());
    if (deviceId_.isEmpty())
        deviceId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void Settings::save() {
    std::lock_guard<std::mutex> g(m_);
    QJsonDocument doc(toJson());
    QFile f(configFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(doc.toJson(QJsonDocument::Indented));
}

QJsonObject Settings::toJson() const {
    QJsonObject root;
    root["deviceId"]       = deviceId_;
    root["jwt"]            = jwt_;
    root["apiKey"]         = apiKey_;
    root["email"]          = accountEmail_;
    root["cachedMinutes"]  = cachedMinutes_;
    root["audioSource"]    = audioSource_;
    root["wizardDone"]     = wizardDone_;

    QJsonObject style;
    style["fontSize"]   = style_.fontSize;
    style["fontFamily"] = style_.fontFamily;
    style["fontColor"]  = style_.fontColor;
    style["bgColor"]    = style_.bgColor;
    style["bgOpacity"]  = style_.bgOpacity;
    style["shadow"]     = style_.shadow;
    style["uppercase"]  = style_.uppercase;
    style["align"]      = style_.align;
    style["position"]   = style_.position;
    style["maxLines"]   = style_.maxLines;
    style["lineHeight"] = style_.lineHeight;
    root["style"] = style;

    QJsonObject lang;
    lang["sourceLanguage"] = lang_.sourceLanguage;
    lang["translateTo"]    = lang_.translateTo;
    root["language"] = lang;

    QJsonObject sess;
    sess["mode"] = sess_.mode;
    root["session"] = sess;
    return root;
}

void Settings::fromJson(const QJsonObject &o) {
    deviceId_      = o.value("deviceId").toString();
    jwt_           = o.value("jwt").toString();
    apiKey_        = o.value("apiKey").toString();
    accountEmail_  = o.value("email").toString();
    cachedMinutes_ = o.value("cachedMinutes").toInt(-1);
    audioSource_   = o.value("audioSource").toString();
    wizardDone_    = o.value("wizardDone").toBool(false);

    auto style = o.value("style").toObject();
    style_.fontSize   = style.value("fontSize").toInt(style_.fontSize);
    style_.fontFamily = style.value("fontFamily").toString(style_.fontFamily);
    style_.fontColor  = style.value("fontColor").toString(style_.fontColor);
    style_.bgColor    = style.value("bgColor").toString(style_.bgColor);
    style_.bgOpacity  = style.value("bgOpacity").toInt(style_.bgOpacity);
    style_.shadow     = style.value("shadow").toBool(style_.shadow);
    style_.uppercase  = style.value("uppercase").toBool(style_.uppercase);
    style_.align      = style.value("align").toString(style_.align);
    style_.position   = style.value("position").toInt(style_.position);
    style_.maxLines   = style.value("maxLines").toInt(style_.maxLines);
    style_.lineHeight = style.value("lineHeight").toInt(style_.lineHeight);

    auto lang = o.value("language").toObject();
    lang_.sourceLanguage = lang.value("sourceLanguage").toString(lang_.sourceLanguage);
    lang_.translateTo    = lang.value("translateTo").toString(lang_.translateTo);

    auto sess = o.value("session").toObject();
    sess_.mode = sess.value("mode").toString(sess_.mode);
}

#define G(name, member, T) T Settings::name() const { std::lock_guard<std::mutex> g(m_); return member; }
#define S(name, member, T) void Settings::name(const T &v) { std::lock_guard<std::mutex> g(m_); member = v; }

G(deviceId,        deviceId_,      QString)
G(jwt,             jwt_,           QString)
S(setJwt,          jwt_,           QString)
G(apiKey,          apiKey_,        QString)
S(setApiKey,       apiKey_,        QString)
G(accountEmail,    accountEmail_,  QString)
S(setAccountEmail, accountEmail_,  QString)
G(audioSourceName, audioSource_,   QString)
S(setAudioSourceName, audioSource_, QString)
G(style,           style_,         CaptionStyle)
S(setStyle,        style_,         CaptionStyle)
G(language,        lang_,          LanguageConfig)
S(setLanguage,     lang_,          LanguageConfig)
G(session,         sess_,          SessionConfig)
S(setSession,      sess_,          SessionConfig)

int  Settings::cachedMinutesRemaining() const { std::lock_guard<std::mutex> g(m_); return cachedMinutes_; }
void Settings::setCachedMinutesRemaining(int v) { std::lock_guard<std::mutex> g(m_); cachedMinutes_ = v; }
bool Settings::wizardCompleted() const { std::lock_guard<std::mutex> g(m_); return wizardDone_; }
void Settings::setWizardCompleted(bool v) { std::lock_guard<std::mutex> g(m_); wizardDone_ = v; }

#undef G
#undef S

} // namespace tiktool
