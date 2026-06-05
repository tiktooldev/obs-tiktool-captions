// TikTool Live Captions — OBS plugin entry point.
//
// Registers a dockable Qt widget the streamer interacts with; everything else
// (audio capture, WS streaming, overlay install, trial tracking) is owned by
// the dock + helpers in the same module.

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QString>

#include "version.h"
#include "captions-dock.hpp"
#include "settings.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-tiktool-captions", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Real-time captions for TikTok Live by TikTool";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "TikTool Live Captions";
}

static tiktool::TikToolCaptionsDock *g_dock = nullptr;

static void on_frontend_loaded(enum obs_frontend_event ev, void *)
{
    if (ev != OBS_FRONTEND_EVENT_FINISHED_LOADING)
        return;
    if (g_dock)
        return;
    g_dock = tiktool::TikToolCaptionsDock::install();
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[tiktool-captions] loading v%s", TIKTOOL_CAPTIONS_VERSION_STR);
    tiktool::Settings::instance().load();
    // Register our bundled data dir so Qt can find the Schannel TLS backend
    // we ship in data/tls/. Without this QSslSocket has no TLS provider in
    // the OBS process and our WS over wss:// would fail to handshake.
    char *dataDir = obs_module_get_config_path(obs_current_module(), "");
    char *modDataDir = obs_module_file("");
    if (modDataDir && *modDataDir) {
        QCoreApplication::addLibraryPath(QString::fromUtf8(modDataDir));
    }
    bfree(dataDir);
    bfree(modDataDir);
    obs_frontend_add_event_callback(on_frontend_loaded, nullptr);
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(on_frontend_loaded, nullptr);
    if (g_dock) {
        g_dock->shutdown();
        g_dock = nullptr;
    }
    tiktool::Settings::instance().save();
    blog(LOG_INFO, "[tiktool-captions] unloaded");
}
