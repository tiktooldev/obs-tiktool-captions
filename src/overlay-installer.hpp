#pragma once

#include <QObject>
#include <QString>
#include "settings.hpp"

namespace tiktok {

// Creates or updates a Browser Source inside the streamer's current OBS scene
// pointing at overlay.tik.tools with their JWT + setup id baked into the URL.
// Geometry defaults to vertical 9:16 (1080x1920) since that's where 99% of
// TikTok LIVE streams render. The browser source is named "TikTok Captions - by TikTools"
// and is reused on repeat runs instead of duplicated.
class OverlayInstaller : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Returns the URL the overlay browser source ended up pointing at.
    // watermark=true forces a small "captions by tik.tools" badge on the
    // rendered overlay; flipped by the server during free-trial sessions.
    QString installOrUpdate(const QString &overlayJwt,
                            const QString &setupId,
                            const CaptionStyle &style,
                            const LanguageConfig &lang,
                            bool watermark);

    void remove();

    static QString defaultSourceName() { return "TikTok Captions - by TikTools"; }

signals:
    void installed(const QString &url);
    void failed(const QString &reason);
};

} // namespace tiktok
