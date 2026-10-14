// SPDX-License-Identifier: GPL-2.0-or-later
// Real, unmodified OBS RTMPS output -> loopback MediaMTX TLS endpoints.
#include "stream/output-manager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <obs-module.h>
#include <stdexcept>

static void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &predicate, int timeout = 15000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (predicate())
            return true;
        QThread::msleep(10);
    } while (timer.elapsed() < timeout);
    return false;
}
static QJsonObject api(int server, const QString &path) {
    QNetworkAccessManager network;
    auto *reply = network.get(
        QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/v3/%2").arg(19997 + server).arg(path))));
    if (!until([&] { return reply->isFinished(); }, 2000)) {
        reply->abort();
        return {};
    }
    if (reply->error() != QNetworkReply::NoError)
        return {};
    return QJsonDocument::fromJson(reply->readAll()).object();
}
static qint64 received(int server) {
    qint64 count = 0;
    for (const auto &item : api(server, "paths/list").value("items").toArray())
        count += item.toObject().value("bytesReceived").toInteger();
    return count;
}
static void module(const QString &root, const char *name) {
    obs_module_t *m = nullptr;
    const auto binary = (root + "/obs-plugins/64bit/" + name + ".dll").toUtf8();
    const auto data = (root + "/data/obs-plugins/" + name).toUtf8();
    require(obs_open_module(&m, binary.constData(), data.constData()) == MODULE_SUCCESS && obs_init_module(m),
            name);
}
struct Servers {
    std::array<QProcess, 3> processes;
    QString executable, fixture;
    Servers(QString exe, QString dir) : executable(std::move(exe)), fixture(std::move(dir)) {}
    void start(int i) {
        auto &p = processes[i];
        p.setWorkingDirectory(fixture);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.setStandardOutputFile(fixture + QString("/server-%1.log").arg(i), QIODevice::Append);
        p.start(executable, {QString("server-%1.yml").arg(i)});
        require(p.waitForStarted(5000), "MediaMTX process started");
        require(until([i, &p] {
                    return p.state() == QProcess::Running && api(i, "config/global/get").contains("rtmp");
                }),
                "loopback API ready");
    }
    void stop(int i) {
        processes[i].kill(); // Fault injection into this test's own server only.
        processes[i].waitForFinished(5000);
    }
    ~Servers() {
        for (int i = 0; i < 3; ++i)
            if (processes[i].state() != QProcess::NotRunning)
                stop(i);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 4) {
        std::cerr << "Usage: rtmps-integration <OBS root> <MediaMTX exe> <fixture dir>\n";
        return 2;
    }
    bool started = false;
    int result = 0;
    try {
        const QString root = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
        Servers servers(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
        for (int i = 0; i < 3; ++i)
            servers.start(i);
        QTemporaryDir temp;
        started = obs_startup("en-US", temp.path().toUtf8().constData(), nullptr);
        require(started, "libobs startup");
        obs_add_data_path((root + "/data/libobs/").toUtf8().constData());
        obs_audio_info audio{};
        audio.samples_per_sec = 48000;
        audio.speakers = SPEAKERS_STEREO;
        require(obs_reset_audio(&audio), "audio clock");
        const auto graphics = (root + "/bin/64bit/libobs-d3d11.dll").toUtf8();
        obs_video_info video{};
        video.graphics_module = graphics.constData();
        video.fps_num = 30;
        video.fps_den = 1;
        video.base_width = 640;
        video.base_height = 360;
        video.output_width = 640;
        video.output_height = 360;
        video.output_format = VIDEO_FORMAT_NV12;
        video.gpu_conversion = true;
        video.colorspace = VIDEO_CS_709;
        video.range = VIDEO_RANGE_PARTIAL;
        video.scale_type = OBS_SCALE_BICUBIC;
        require(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "D3D11 video clock");
        for (const auto *name : {"obs-x264", "obs-ffmpeg", "rtmp-services", "obs-outputs"})
            module(root, name);
        sb::OutputManager manager(temp.path()); // Default production rtmp_output, no test substitution.
        QString error;
        require(manager.secrets().put("YouTube/stream-key", "test-stream", error),
                "first fixture credential");
        require(manager.secrets().put("Twitch/stream-key", "test-stream", error),
                "second fixture credential");
        auto config = manager.config();
        config.endpoints[0].server = "rtmps://localhost:19443/live";
        config.endpoints[1].server = "rtmps://localhost:19444/live";
        config.common.bitrate = 500;
        require(manager.configure(config, error), "RTMPS configuration");
        auto bothLive = [&] {
            return manager.status(0).state == sb::StreamState::Live &&
                   manager.status(1).state == sb::StreamState::Live;
        };
        manager.startAll();
        require(until(bothLive), "two real RTMPS connections live");
        require(obs_output_get_video_encoder(manager.output(0)) ==
                    obs_output_get_video_encoder(manager.output(1)),
                "RTMPS shared video encoder");
        require(until([] { return received(0) > 1000 && received(1) > 1000; }),
                "both TLS servers receive media bytes");
        for (int i = 0; i < 2; ++i) {
            const auto items = api(i, "paths/list").value("items").toArray();
            require(items.size() == 1 && items[0].toObject().value("ready").toBool(),
                    "server reports ready stream");
            std::cout << "SERVER " << i << " "
                      << QJsonDocument(items[0].toObject()).toJson(QJsonDocument::Compact).constData()
                      << std::endl;
        }
        manager.stop(1);
        require(until([&] { return manager.status(1).state == sb::StreamState::Idle; }),
                "individual RTMPS stop");
        auto baseline = received(0);
        require(until([&] { return received(0) > baseline + 1000; }),
                "peer receives after other output stops");
        manager.start(1);
        require(until(bothLive), "RTMPS rejoin");
        servers.stop(1);
        require(until([&] { return manager.status(1).state == sb::StreamState::Backoff; }),
                "network loss triggers bounded backoff");
        baseline = received(0);
        require(until([&] { return received(0) > baseline + 1000; }), "peer survives endpoint process crash");
        servers.start(1);
        require(until(bothLive, 30000), "RTMPS automatic reconnection");
        require(manager.status(0).reconnects == 0 && manager.status(1).reconnects > 0,
                "only failed endpoint reconnects");
        manager.stopAll();
        require(until([&] { return !manager.busy(); }), "both RTMPS outputs stop");
        config.advanced = true;
        config.endpoints[0].encoder.bitrate = 700;
        config.endpoints[1].encoder.bitrate = 300;
        config.endpoints[1].encoder.width = 320;
        config.endpoints[1].encoder.height = 180;
        config.endpoints[1].encoder.divisor = 2;
        require(manager.configure(config, error), "advanced RTMPS configuration");
        manager.startAll();
        require(until(bothLive), "advanced RTMPS connections");
        require(until([] { return received(0) > 1000 && received(1) > 1000; }),
                "advanced media delivered over TLS");
        require(obs_output_get_video_encoder(manager.output(0)) !=
                    obs_output_get_video_encoder(manager.output(1)),
                "advanced encoders independent");
        manager.stopAll();
        require(until([&] { return !manager.busy(); }), "advanced stop");
        config.endpoints[1].server = "rtmps://localhost:19445/live";
        require(manager.configure(config, error), "untrusted TLS endpoint configured");
        manager.startAll();
        require(until([&] {
                    return manager.status(0).state == sb::StreamState::Live &&
                           manager.status(1).state == sb::StreamState::Backoff;
                }),
                "untrusted certificate rejected while trusted peer is live");
        require(received(2) == 0, "untrusted TLS endpoint received no media");
        manager.stopAll();
        require(until([&] { return !manager.busy(); }), "backoff cancelled");
        config.endpoints[1].server = "rtmps://localhost:19444/live";
        require(manager.configure(config, error), "restore trusted endpoint");
        manager.start(1);
        require(until([&] { return manager.status(1).state == sb::StreamState::Live; }),
                "single endpoint launch");
        require(manager.status(0).state == sb::StreamState::Idle, "single launch leaves other stopped");
        manager.shutdown();
        require(!manager.busy(), "shutdown during active RTMPS");
        std::cout << "PASS: real RTMPS acceptance complete; no platform accounts used\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        result = 1;
    }
    if (started)
        obs_shutdown();
    return result;
}
