// SPDX-License-Identifier: GPL-2.0-or-later
#include "browser/source.hpp"
#include "stream/output-manager.hpp"
#include "ui/dock.hpp"
#include "ui/locale.hpp"
#include "ui/platform-docks.hpp"
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
    return "AugustusNobilis and StreamBridge contributors";
}

static std::unique_ptr<sb::OutputManager> manager;
static QPointer<sb::Dock> dock;
static std::unique_ptr<sb::PlatformHub> hub;
static QPointer<QWidget> chatDock, infoDock;
static QPointer<QAction> menu;
static bool exited = false;
static void frontendEvent(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        exited = true;
        if (hub)
            hub->shutdown();
        if (manager)
            manager->shutdown();
        sb::shutdownUpdates();
    }
}
bool obs_module_load(void) {
    if ((obs_get_version() >> 24) < 32)
        return false;
    if (char *path = obs_module_config_path("")) {
        sb::initializeLocale(QString::fromUtf8(path));
        bfree(path);
    }
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
    hub = std::make_unique<sb::PlatformHub>(manager->account(0), manager->account(1));
    QObject::connect(hub.get(), &sb::PlatformHub::youtubeBroadcastEnded, manager.get(),
                     [] { manager->stop(0); });
    infoDock = sb::createInfoDock(*hub, QString::fromUtf8(path));
    sb::initializeUpdates(QString::fromUtf8(path));
    auto *checker = sb::updateChecker();
    const auto canRestart = [] {
        return manager && !manager->busy() && !obs_frontend_streaming_active() &&
               !obs_frontend_recording_active() && !obs_frontend_replay_buffer_active() &&
               !obs_frontend_virtualcam_active();
    };
    checker->setRestartGuard(canRestart);
    QObject::connect(checker, &sb::UpdateChecker::restartRequested, checker, [checker, canRestart] {
        QTimer::singleShot(0, checker, [checker, canRestart] {
            QPointer<sb::UpdateChecker> alive(checker);
            auto *window = static_cast<QWidget *>(obs_frontend_get_main_window());
            if (!canRestart() || !window || !window->close()) {
                if (alive)
                    alive->cancelRestart();
            }
        });
    });
    bfree(path);
    dock = new sb::Dock(*manager, *hub);
    if (!obs_frontend_add_dock_by_id("streambridge", "StreamBridge", dock)) {
        delete dock;
        delete infoDock;
        hub.reset();
        manager.reset();
        return;
    }
    chatDock = sb::createChatDock(*hub);
    if (!obs_frontend_add_dock_by_id("streambridge-chat", sb::text("ChatDockTitle").toUtf8().constData(),
                                     chatDock))
        delete chatDock;
    if (!obs_frontend_add_dock_by_id("streambridge-info", sb::text("InfoDockTitle").toUtf8().constData(),
                                     infoDock))
        delete infoDock;
    menu = static_cast<QAction *>(
        obs_frontend_add_tools_menu_qaction(sb::text("Settings").toUtf8().constData()));
    QObject::connect(menu, &QAction::triggered, dock, &sb::Dock::openSettings);
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    blog(LOG_INFO, "[StreamBridge] %s loaded", STREAMBRIDGE_VERSION);
}
void obs_module_unload(void) {
    if (hub)
        hub->shutdown();
    if (!exited)
        obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (manager)
        manager->shutdown();
    if (!exited && dock)
        obs_frontend_remove_dock("streambridge");
    if (!exited && chatDock)
        obs_frontend_remove_dock("streambridge-chat");
    if (!exited && infoDock)
        obs_frontend_remove_dock("streambridge-info");
    if (menu)
        delete menu;
    hub.reset();
    manager.reset();
    sb::shutdownUpdates();
    sb::shutdownLocale();
}
