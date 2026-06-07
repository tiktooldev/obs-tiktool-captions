#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <mutex>

namespace tiktok {

// User-tunable + persisted state. Lives on disk via OBS module config dir;
// authoritative copies for paid users get synced server-side through the
// existing captions-overlay setups storage (TikTok cloud).
struct CaptionStyle {
    int  fontSize      = 56;
    QString fontFamily = "Inter";
    QString fontColor  = "#FFFFFF";
    QString bgColor    = "#000000";
    int  bgOpacity     = 0;          // 0..100
    bool shadow        = true;
    bool uppercase     = false;
    QString align      = "center";   // left | center | right
    int  position      = 75;          // 0..100, vertical %
    int  maxLines      = 2;
    int  lineHeight    = 120;         // % of font size
    // Additional surfaces that mirror the captions-overlay editor on the
    // website so the streamer sees the same controls in both places.
    QString bgMode     = "shadow";   // none | shadow | box
    int  xpad          = 4;           // horizontal padding %
    bool showOriginal  = false;       // show source-language line alongside translation
    bool showLangLabels= false;       // prefix lines with "EN ->"
    bool speakerNames  = false;       // prefix lines with diarized speaker name
    bool speakerColors = false;       // color each speaker differently
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
    // Wipes everything except the persisted device id (we keep that so the
    // anti-abuse fingerprint mapping survives a manual reset; otherwise a
    // streamer could spam Reset to dodge duplicate-account detection).
    void resetAll();

    // Authentication / identity
    QString deviceId() const;        // stable per-install UUID, persisted
    // Hardware fingerprint = SHA-256(machineGuid + computerName + volumeSerial).
    // Stable across plugin reinstalls / OBS reinstalls / device-id resets.
    // Used by the trial-check anti-abuse layer to catch the same machine
    // signing up under a different account. Falls back to deviceId on
    // platforms where the hardware probe fails. Cached after first call.
    QString fingerprint() const;
    QString jwt() const;             // captions JWT once user logs in
    void    setJwt(const QString &);
    QString apiKey() const;
    void    setApiKey(const QString &);
    QString accountEmail() const;
    void    setAccountEmail(const QString &);
    QString tiktokUsername() const;   // streamer's @username (no @), captions hub key
    void    setTiktokUsername(const QString &);

    // Trial state cache (server is source of truth, this is just UX)
    int     cachedMinutesRemaining() const;
    void    setCachedMinutesRemaining(int);

    // Audio - primary mic
    QString audioSourceName() const;
    void    setAudioSourceName(const QString &);
    QString audioDeviceId() const;             // wasapi/coreaudio/pulse id
    void    setAudioDeviceId(const QString &);
    QString audioMode() const;                 // "system" (direct device) or "obs-source"
    void    setAudioMode(const QString &);
    // Co-host audio - opponent / battle voice. Same device the streamer
    // listens to during co-host on TikTok LIVE Studio. Default: system
    // loopback so the streamer hears it AND we transcribe it.
    QString cohostSourceName() const;
    void    setCohostSourceName(const QString &);
    QString cohostDeviceId() const;
    void    setCohostDeviceId(const QString &);
    QString cohostMode() const;                // "off" | "system" | "obs-source"
    void    setCohostMode(const QString &);

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

    // Persistent streaming state. When the streamer hits Start, we flip this
    // to true so a later OBS restart resumes captions automatically. Stop
    // flips it back. Auto-resume only fires if (a) we have a JWT, (b) trial
    // minutesLeft > 0 or sub is active, (c) an audio device is bound.
    bool streamingPersisted() const;
    void setStreamingPersisted(bool);

    // UI scale: 0.85 / 1.00 / 1.20 / 1.40. Drives the dock font size + padding
    // so streamers on small laptops can shrink the layout to one column.
    double uiScale() const;
    void   setUiScale(double);

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
    QString tiktokUsername_;
    int     cachedMinutes_ = -1;
    QString audioSource_;
    QString audioDeviceId_;
    QString audioMode_ = "system"; // default to picking a real system mic
    QString cohostSource_;
    QString cohostDeviceId_;
    QString cohostMode_ = "off";
    CaptionStyle    style_;
    LanguageConfig  lang_;
    SessionConfig   sess_;
    bool    wizardDone_ = false;
    bool    streamingPersisted_ = false;
    double  uiScale_ = 1.0;
    mutable QString cachedFingerprint_;
};

} // namespace tiktok
