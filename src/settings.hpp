#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <mutex>

namespace tiktool {

// User-tunable + persisted state. Lives on disk via OBS module config dir;
// authoritative copies for paid users get synced server-side through the
// existing captions-overlay setups storage (ClickHouse + Dragonfly cache).
struct CaptionStyle {
    int  fontSize     = 56;
    QString fontFamily = "Inter";
    QString fontColor  = "#FFFFFF";
    QString bgColor    = "#000000";
    int  bgOpacity    = 0;          // 0..100
    bool shadow       = true;
    bool uppercase    = false;
    QString align      = "center";   // left | center | right
    int  position     = 75;          // 0..100, vertical %
    int  maxLines     = 2;
    int  lineHeight   = 120;         // % of font size
};

struct LanguageConfig {
    QString sourceLanguage = "auto"; // ISO 639-1 or "auto"
    QString translateTo    = "off";  // "off" or ISO code
};

struct SessionConfig {
    QString mode = "transcribe_only"; // transcribe_only | translate
};

class Settings {
public:
    static Settings &instance();

    void load();
    void save();

    // Authentication / identity
    QString deviceId() const;        // stable per-install UUID, persisted
    QString jwt() const;             // captions JWT once user logs in
    void    setJwt(const QString &);
    QString apiKey() const;
    void    setApiKey(const QString &);
    QString accountEmail() const;
    void    setAccountEmail(const QString &);

    // Trial state cache (server is source of truth, this is just UX)
    int     cachedMinutesRemaining() const;
    void    setCachedMinutesRemaining(int);

    // Audio
    QString audioSourceName() const;
    void    setAudioSourceName(const QString &);

    // Visual + language
    CaptionStyle    style() const;
    void            setStyle(const CaptionStyle &);
    LanguageConfig  language() const;
    void            setLanguage(const LanguageConfig &);
    SessionConfig   session() const;
    void            setSession(const SessionConfig &);

    // Wizard
    bool wizardCompleted() const;
    void setWizardCompleted(bool);

    QString configFilePath() const;

private:
    Settings();
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &);

    mutable std::mutex m_;
    QString deviceId_;
    QString jwt_;
    QString apiKey_;
    QString accountEmail_;
    int     cachedMinutes_ = -1;
    QString audioSource_;
    CaptionStyle    style_;
    LanguageConfig  lang_;
    SessionConfig   sess_;
    bool    wizardDone_ = false;
};

} // namespace tiktool
