// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit opt-in test-only module, never installed or packaged. Uses local synthetic media only.
#include "stream/output-manager.hpp"
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <memory>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>
OBS_DECLARE_MODULE()
namespace {
struct Sink {
    obs_output_t *output;
    std::atomic<uint64_t> bytes{0};
};
QString reportPath;
std::unique_ptr<sb::OutputManager> manager;
std::unique_ptr<QTemporaryDir> directory;
std::unique_ptr<QTimer> timer;
obs_service_t *originalService = nullptr;
obs_output_t *primary = nullptr;
QElapsedTimer elapsed, phaseTime;
int phase = 0;
uint64_t mainBytes = 0, pluginBytes = 0;
bool passing = false, ending = false, exited = false;
void report(const QString &message) {
    QFile file(reportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(message.toUtf8() + '\n');
        file.flush();
    }
    blog(LOG_INFO, "[StreamBridge frontend test] %s", message.toUtf8().constData());
}
void finish(bool success, const char *reason) {
    if (ending)
        return;
    ending = true;
    passing = success;
    timer->stop();
    report(QString(success ? "PASS: " : "FAIL: ") + reason);
    // Release observation references before OBS tears down its own outputs.
    if (primary) {
        obs_output_release(primary);
        primary = nullptr;
    }
    if (originalService) {
        obs_service_release(originalService);
        originalService = nullptr;
    }
    // Exercise OBS's own close path, including stopping an active standard stream.
    QTimer::singleShot(0, qApp, [] { static_cast<QWidget *>(obs_frontend_get_main_window())->close(); });
}
uint64_t bytes(size_t index) {
    auto *output = manager->output(index);
    return output ? obs_output_get_total_bytes(output) : 0;
}
void next(int value) {
    phase = value;
    phaseTime.restart();
}
void tick() {
    if (elapsed.elapsed() > 60000) {
        finish(false, "frontend test timed out");
        return;
    }
    const bool active = obs_frontend_streaming_active();
    switch (phase) {
    case 0:
        if (!active)
            return;
        primary = obs_frontend_get_streaming_output();
        if (!primary || obs_output_get_total_bytes(primary) < 2000) {
            if (primary)
                obs_output_release(primary);
            primary = nullptr;
            return;
        }
        report("PASS: standard OBS frontend streaming sends real local RTMP media");
        manager->startAll();
        next(1);
        break;
    case 1:
        if (manager->status(0).state != sb::StreamState::Live ||
            manager->status(1).state != sb::StreamState::Live || bytes(0) < 2000 || bytes(1) < 2000)
            return;
        if (!active ||
            obs_output_get_video_encoder(primary) == obs_output_get_video_encoder(manager->output(0))) {
            finish(false, "frontend and plugin encoders are not independent");
            return;
        }
        report(
            "PASS: standard stream and two plugin encoded outputs coexist with separate encoder ownership");
        mainBytes = obs_output_get_total_bytes(primary);
        pluginBytes = bytes(0);
        manager->stop(1);
        next(2);
        break;
    case 2:
        if (phaseTime.elapsed() < 1500 || manager->status(1).state != sb::StreamState::Idle)
            return;
        if (!active || obs_output_get_total_bytes(primary) <= mainBytes || bytes(0) <= pluginBytes) {
            finish(false, "stopping one plugin output interrupted a peer");
            return;
        }
        report("PASS: stopping plugin Twitch leaves standard OBS stream and YouTube sending media");
        pluginBytes = bytes(0);
        obs_frontend_streaming_stop();
        next(3);
        break;
    case 3:
        if (active || phaseTime.elapsed() < 1500)
            return;
        if (manager->status(0).state != sb::StreamState::Live || bytes(0) <= pluginBytes) {
            finish(false, "standard stop interrupted plugin stream");
            return;
        }
        report("PASS: standard OBS Stop Streaming leaves plugin encoder/output live");
        obs_output_release(primary);
        primary = nullptr;
        obs_frontend_streaming_start();
        next(4);
        break;
    case 4:
        if (!active)
            return;
        primary = obs_frontend_get_streaming_output();
        if (!primary || obs_output_get_total_bytes(primary) < 2000) {
            if (primary)
                obs_output_release(primary);
            primary = nullptr;
            return;
        }
        report("PASS: standard OBS Start Streaming restarts while plugin output is live");
        manager->start(1);
        next(5);
        break;
    case 5:
        if (manager->status(1).state != sb::StreamState::Live || bytes(1) < 2000)
            return;
        if (!active) {
            finish(false, "standard stream lost before shutdown");
            return;
        }
        finish(true, "requesting OBS close with standard stream and both plugin outputs active");
        break;
    }
}
void start() {
    auto *window = static_cast<QWidget *>(obs_frontend_get_main_window());
    for (const auto *id : {"streambridge", "streambridge-chat", "streambridge-info"}) {
        if (!window->findChild<QDockWidget *>(id)) {
            finish(false, "production StreamBridge dock missing");
            return;
        }
    }
    report("PASS: all three production StreamBridge docks registered in OBS");
    auto *browserSource =
        obs_source_create_private("streambridge_browser", "Unconfigured browser lifecycle test", nullptr);
    if (!browserSource) {
        finish(false, "production browser source registration failed");
        return;
    }
    obs_source_release(browserSource);
    report("PASS: production Proxied Browser Source creates and destroys inside OBS");
    directory = std::make_unique<QTemporaryDir>();
    manager = std::make_unique<sb::OutputManager>(directory->path(), nullptr, "streambridge_frontend_sink");
    QString error;
    if (!directory->isValid() ||
        !manager->secrets().put("YouTube/stream-key", "synthetic-local-only", error) ||
        !manager->secrets().put("Twitch/stream-key", "synthetic-local-only", error)) {
        finish(false, "test vault failed");
        return;
    }
    auto config = manager->config();
    config.common.bitrate = 500;
    if (!manager->configure(config, error)) {
        finish(false, "test encoder config failed");
        return;
    }
    originalService = obs_frontend_get_streaming_service();
    if (originalService)
        originalService = obs_service_get_ref(originalService);
    auto *settings = obs_data_create();
    obs_data_set_string(settings, "server", "rtmp://127.0.0.1:19365/live");
    obs_data_set_string(settings, "key", "frontend-fixture");
    auto *service = obs_service_create("rtmp_custom", "StreamBridge local frontend test", settings, nullptr);
    obs_data_release(settings);
    if (!service) {
        finish(false, "local frontend service creation failed");
        return;
    }
    obs_frontend_set_streaming_service(service);
    obs_service_release(service);
    elapsed.start();
    phaseTime.start();
    timer->start(100);
    obs_frontend_streaming_start();
}
void event(enum obs_frontend_event e, void *) {
    if (e == OBS_FRONTEND_EVENT_FINISHED_LOADING)
        QTimer::singleShot(1000, qApp, start);
    if (e == OBS_FRONTEND_EVENT_EXIT) {
        exited = true;
        if (timer)
            timer->stop();
        if (manager)
            manager->shutdown();
        report(passing ? "PASS: OBS frontend EXIT shuts down plugin outputs"
                       : "FAIL: OBS exited before successful test");
    }
}
} // namespace
bool obs_module_load(void) {
    char *path = obs_module_file("run.txt");
    QFile marker(QString::fromUtf8(path ? path : ""));
    bfree(path);
    if (!marker.open(QIODevice::ReadOnly) ||
        marker.readAll().trimmed() != "STREAMBRIDGE_LOCAL_FRONTEND_TEST_V1")
        return false;
    reportPath = marker.fileName() + ".log";
    QFile::remove(reportPath);
    obs_output_info info{};
    info.id = "streambridge_frontend_sink";
    info.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED;
    info.get_name = [](void *) { return "Local frontend test sink"; };
    info.create = [](obs_data_t *, obs_output_t *o) -> void * { return new Sink{o}; };
    info.destroy = [](void *p) { delete static_cast<Sink *>(p); };
    info.start = [](void *p) {
        auto *s = static_cast<Sink *>(p);
        return obs_output_can_begin_data_capture(s->output, 0) &&
               obs_output_initialize_encoders(s->output, 0) && obs_output_begin_data_capture(s->output, 0);
    };
    info.stop = [](void *p, uint64_t) { obs_output_end_data_capture(static_cast<Sink *>(p)->output); };
    info.encoded_packet = [](void *p, encoder_packet *packet) {
        if (packet)
            static_cast<Sink *>(p)->bytes += packet->size;
    };
    info.get_total_bytes = [](void *p) { return static_cast<Sink *>(p)->bytes.load(); };
    info.encoded_video_codecs = "h264";
    info.encoded_audio_codecs = "aac";
    obs_register_output(&info);
    timer = std::make_unique<QTimer>();
    QObject::connect(timer.get(), &QTimer::timeout, qApp, tick);
    obs_frontend_add_event_callback(event, nullptr);
    return true;
}
void obs_module_unload(void) {
    if (!exited)
        obs_frontend_remove_event_callback(event, nullptr);
    timer.reset();
    manager.reset();
    if (primary)
        obs_output_release(primary);
    if (originalService)
        obs_service_release(originalService);
    directory.reset();
    report(passing ? "PASS: test module unloaded cleanly" : "FAIL: test module unloaded");
}
