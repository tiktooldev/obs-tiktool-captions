#include "settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace tiktok {

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
        QDir().mkpath(base + "/obs-tiktok-captions");
        out = base + "/obs-tiktok-captions/settings.json";
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

void Settings::resetAll() {
    QString preservedDevice;
    {
        std::lock_guard<std::mutex> g(m_);
        preservedDevice = deviceId_;
        jwt_.clear();
        apiKey_.clear();
        accountEmail_.clear();
        tiktokUsername_.clear();
        cachedMinutes_ = -1;
        audioSource_.clear();
        audioDeviceId_.clear();
        audioMode_   = "system";
        cohostSource_.clear();
        cohostDeviceId_.clear();
        cohostMode_  = "off";
        style_       = CaptionStyle{};
        lang_        = LanguageConfig{};
        sess_        = SessionConfig{};
        wizardDone_  = false;
        streamingPersisted_ = false;
        uiScale_ = 1.0;
        cachedFingerprint_.clear();
    }
    save();
    // Re-fingerprint on next access (cached_ was cleared above).
    Q_UNUSED(preservedDevice);
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
    root["tiktokUsername"] = tiktokUsername_;
    root["cachedMinutes"]  = cachedMinutes_;
    root["audioSource"]    = audioSource_;
    root["audioDeviceId"]  = audioDeviceId_;
    root["audioMode"]      = audioMode_;
    root["cohostSource"]   = cohostSource_;
    root["cohostDeviceId"] = cohostDeviceId_;
    root["cohostMode"]     = cohostMode_;
    root["wizardDone"]         = wizardDone_;
    root["streamingPersisted"] = streamingPersisted_;
    root["uiScale"]            = uiScale_;

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
    style["bgMode"]        = style_.bgMode;
    style["xpad"]          = style_.xpad;
    style["showOriginal"]  = style_.showOriginal;
    style["showLangLabels"]= style_.showLangLabels;
    style["speakerNames"]  = style_.speakerNames;
    style["speakerColors"] = style_.speakerColors;
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
    accountEmail_   = o.value("email").toString();
    tiktokUsername_ = o.value("tiktokUsername").toString();
    cachedMinutes_ = o.value("cachedMinutes").toInt(-1);
    audioSource_   = o.value("audioSource").toString();
    audioDeviceId_ = o.value("audioDeviceId").toString();
    audioMode_      = o.value("audioMode").toString(audioMode_);
    cohostSource_   = o.value("cohostSource").toString();
    cohostDeviceId_ = o.value("cohostDeviceId").toString();
    cohostMode_     = o.value("cohostMode").toString(cohostMode_);
    wizardDone_          = o.value("wizardDone").toBool(false);
    streamingPersisted_  = o.value("streamingPersisted").toBool(false);
    uiScale_             = o.value("uiScale").toDouble(1.0);
    if (uiScale_ < 0.5 || uiScale_ > 2.0) uiScale_ = 1.0;

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
    style_.bgMode         = style.value("bgMode").toString(style_.bgMode);
    style_.xpad           = style.value("xpad").toInt(style_.xpad);
    style_.showOriginal   = style.value("showOriginal").toBool(style_.showOriginal);
    style_.showLangLabels = style.value("showLangLabels").toBool(style_.showLangLabels);
    style_.speakerNames   = style.value("speakerNames").toBool(style_.speakerNames);
    style_.speakerColors  = style.value("speakerColors").toBool(style_.speakerColors);

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
G(tiktokUsername,    tiktokUsername_, QString)
S(setTiktokUsername, tiktokUsername_, QString)
G(audioSourceName, audioSource_,   QString)
S(setAudioSourceName, audioSource_, QString)
G(audioDeviceId,    audioDeviceId_, QString)
S(setAudioDeviceId, audioDeviceId_, QString)
G(audioMode,        audioMode_,     QString)
S(setAudioMode,     audioMode_,     QString)
G(cohostSourceName, cohostSource_,   QString)
S(setCohostSourceName, cohostSource_, QString)
G(cohostDeviceId,   cohostDeviceId_, QString)
S(setCohostDeviceId,cohostDeviceId_, QString)
G(cohostMode,       cohostMode_,     QString)
S(setCohostMode,    cohostMode_,     QString)
G(style,           style_,         CaptionStyle)
S(setStyle,        style_,         CaptionStyle)
G(language,        lang_,          LanguageConfig)
S(setLanguage,     lang_,          LanguageConfig)
G(session,         sess_,          SessionConfig)
S(setSession,      sess_,          SessionConfig)

#if defined(_WIN32)
// Reads HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid - the Windows-issued
// per-install machine identifier. Stable across plugin reinstalls; rotates
// only on full OS reinstall or registry tamper.
static QString readMachineGuid() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Cryptography",
        0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS) return {};
    wchar_t buf[256] = {0};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hk, L"MachineGuid", nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS) return {};
    return QString::fromWCharArray(buf).trimmed();
}

static DWORD readSystemDriveVolumeSerial() {
    DWORD serial = 0;
    if (!GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        return 0;
    return serial;
}

static QString readComputerName() {
    wchar_t buf[256] = {0};
    DWORD sz = sizeof(buf) / sizeof(buf[0]);
    if (!GetComputerNameW(buf, &sz)) return {};
    return QString::fromWCharArray(buf, sz);
}
#endif

QString Settings::fingerprint() const {
    {
        std::lock_guard<std::mutex> g(m_);
        if (!cachedFingerprint_.isEmpty()) return cachedFingerprint_;
    }
    QByteArray pool;
#if defined(_WIN32)
    pool += "win:";
    pool += readMachineGuid().toUtf8();
    pool += '|';
    pool += QByteArray::number(quint64(readSystemDriveVolumeSerial()));
    pool += '|';
    pool += readComputerName().toUtf8();
#elif defined(__APPLE__)
    pool += "mac:";
    pool += QSysInfo::machineUniqueId();
#else
    pool += "lnx:";
    pool += QSysInfo::machineUniqueId();
#endif
    pool += '|';
    pool += QSysInfo::productType().toUtf8();
    pool += '|';
    pool += QSysInfo::currentCpuArchitecture().toUtf8();

    QByteArray fp;
    if (!pool.isEmpty()) {
        fp = QCryptographicHash::hash(pool, QCryptographicHash::Sha256).toHex();
    }
    std::lock_guard<std::mutex> g(m_);
    if (fp.isEmpty()) {
        // Hardware probe failed - fall back to the install UUID so the server
        // still has something stable to dedup on.
        cachedFingerprint_ = deviceId_;
    } else {
        cachedFingerprint_ = QString::fromLatin1(fp);
    }
    return cachedFingerprint_;
}

int  Settings::cachedMinutesRemaining() const { std::lock_guard<std::mutex> g(m_); return cachedMinutes_; }
void Settings::setCachedMinutesRemaining(int v) { std::lock_guard<std::mutex> g(m_); cachedMinutes_ = v; }
bool Settings::wizardCompleted() const { std::lock_guard<std::mutex> g(m_); return wizardDone_; }
void Settings::setWizardCompleted(bool v) { std::lock_guard<std::mutex> g(m_); wizardDone_ = v; }
bool Settings::streamingPersisted() const { std::lock_guard<std::mutex> g(m_); return streamingPersisted_; }
void Settings::setStreamingPersisted(bool v) { std::lock_guard<std::mutex> g(m_); streamingPersisted_ = v; }
double Settings::uiScale() const { std::lock_guard<std::mutex> g(m_); return uiScale_; }
void   Settings::setUiScale(double v) {
    if (v < 0.5) v = 0.5; if (v > 2.0) v = 2.0;
    std::lock_guard<std::mutex> g(m_); uiScale_ = v;
}

#undef G
#undef S

} // namespace tiktok
