#include "overlay-installer.hpp"
#include "version.h"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QUrl>
#include <QUrlQuery>

namespace tiktool {

static QString buildOverlayUrl(const QString &jwt, const QString &setupId,
                               const CaptionStyle &style, const LanguageConfig &lang) {
    QUrl url(QString::fromLatin1(TIKTOOL_OVERLAY_BASE) + "/");
    QUrlQuery q;
    if (!jwt.isEmpty()) q.addQueryItem("jwt", jwt);
    if (!setupId.isEmpty()) q.addQueryItem("setup", setupId);
    q.addQueryItem("fontSize", QString::number(style.fontSize));
    q.addQueryItem("fontFamily", style.fontFamily);
    q.addQueryItem("fontColor", style.fontColor);
    q.addQueryItem("bgColor", style.bgColor);
    q.addQueryItem("bgOpacity", QString::number(style.bgOpacity));
    q.addQueryItem("shadow", style.shadow ? "1" : "0");
    q.addQueryItem("uppercase", style.uppercase ? "1" : "0");
    q.addQueryItem("align", style.align);
    q.addQueryItem("position", QString::number(style.position));
    q.addQueryItem("maxLines", QString::number(style.maxLines));
    q.addQueryItem("lineHeight", QString::number(style.lineHeight));
    if (!lang.translateTo.isEmpty() && lang.translateTo != "off")
        q.addQueryItem("translateTo", lang.translateTo);
    q.addQueryItem("source", "obs-plugin");
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

QString OverlayInstaller::installOrUpdate(const QString &jwt, const QString &setupId,
                                          const CaptionStyle &style, const LanguageConfig &lang) {
    const QString url = buildOverlayUrl(jwt, setupId, style, lang);
    const QByteArray nameUtf8 = defaultSourceName().toUtf8();
    const QByteArray urlUtf8  = url.toUtf8();

    // Reuse existing source if present, else create a new one in the current scene.
    obs_source_t *existing = obs_get_source_by_name(nameUtf8.constData());
    if (existing) {
        obs_data_t *settings = obs_source_get_settings(existing);
        obs_data_set_string(settings, "url", urlUtf8.constData());
        obs_data_set_int(settings, "width", 1080);
        obs_data_set_int(settings, "height", 1920);
        obs_data_set_bool(settings, "reroute_audio", false);
        obs_data_set_bool(settings, "restart_when_active", false);
        obs_data_set_int(settings, "fps", 30);
        obs_source_update(existing, settings);
        obs_data_release(settings);
        obs_source_release(existing);
        emit installed(url);
        return url;
    }

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "url", urlUtf8.constData());
    obs_data_set_int(settings, "width", 1080);
    obs_data_set_int(settings, "height", 1920);
    obs_data_set_int(settings, "fps", 30);
    obs_data_set_bool(settings, "reroute_audio", false);
    obs_data_set_bool(settings, "restart_when_active", false);

    obs_source_t *src = obs_source_create("browser_source", nameUtf8.constData(), settings, nullptr);
    obs_data_release(settings);
    if (!src) {
        emit failed("Could not create browser source. Is the browser source plugin enabled in OBS?");
        return {};
    }

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) {
        obs_source_release(src);
        emit failed("No active scene in OBS to install the overlay into.");
        return {};
    }
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    if (!scene) {
        obs_source_release(sceneSrc);
        obs_source_release(src);
        emit failed("Could not access the active OBS scene.");
        return {};
    }
    obs_scene_atomic_update(scene, [](void *param, obs_scene_t *s) {
        auto *p = static_cast<obs_source_t *>(param);
        obs_scene_add(s, p);
    }, src);

    obs_source_release(sceneSrc);
    obs_source_release(src);
    emit installed(url);
    return url;
}

void OverlayInstaller::remove() {
    obs_source_t *existing = obs_get_source_by_name(defaultSourceName().toUtf8().constData());
    if (!existing) return;
    obs_source_remove(existing);
    obs_source_release(existing);
}

} // namespace tiktool
