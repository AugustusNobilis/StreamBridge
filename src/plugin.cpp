// SPDX-License-Identifier: GPL-2.0-or-later
#include "browser/source.hpp"
#include "stream/output-manager.hpp"
#include "ui/dock.hpp"
#include "ui/locale.hpp"
#include "updates/checker.hpp"
#include <QAction>
#include <QCoreApplication>
#include <QPointer>
#include <memory>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-streambridge", "en-US")
MODULE_EXPORT const char *obs_module_description(void) {
    return "Independent local RTMPS outputs for YouTube and Twitch";
}
MODULE_EXPORT const char *obs_module_name(void) {
    return "StreamBridge";
}
MODULE_EXPORT const char *obs_module_author(void) {
    return "StreamBridge contributors";
}

static std::unique_ptr<sb::OutputManager> manager;
static QPointer<sb::Dock> dock;
static QPointer<QAction> menu;
static bool exited = false;
static void frontendEvent(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        exited = true;
        if (manager)
            manager->shutdown();
        sb::shutdownUpdates();
    }
}
bool obs_module_load(void) {
    if ((obs_get_version() >> 24) < 32)
        return false;
    // OBS does not necessarily ship a TLS backend for its Qt Network DLL.
    char *tlsPath = obs_module_file("qt-plugins");
    if (tlsPath) {
        QCoreApplication::addLibraryPath(QString::fromUtf8(tlsPath));
        bfree(tlsPath);
    }
    sb::registerBrowserSource();
    return true;
}
void obs_module_post_load(void) {
    char *path = obs_module_config_path("");
    if (!path)
        return;
    manager = std::make_unique<sb::OutputManager>(QString::fromUtf8(path));
    sb::initializeUpdates(QString::fromUtf8(path));
    bfree(path);
    dock = new sb::Dock(*manager);
    if (!obs_frontend_add_dock_by_id("streambridge", "StreamBridge", dock)) {
        delete dock;
        manager.reset();
        return;
    }
    menu = static_cast<QAction *>(
        obs_frontend_add_tools_menu_qaction(sb::text("Settings").toUtf8().constData()));
    QObject::connect(menu, &QAction::triggered, dock, &sb::Dock::openSettings);
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    blog(LOG_INFO, "[StreamBridge] %s loaded", STREAMBRIDGE_VERSION);
}
void obs_module_unload(void) {
    if (!exited)
        obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (manager)
        manager->shutdown();
    if (!exited && dock)
        obs_frontend_remove_dock("streambridge");
    if (menu)
        delete menu;
    manager.reset();
    sb::shutdownUpdates();
}
