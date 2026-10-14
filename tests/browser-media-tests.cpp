// SPDX-License-Identifier: GPL-2.0-or-later
#include "browser/host.hpp"
#include "browser/settings.hpp"
#include "browser/worker.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QUdpSocket>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <obs-module.h>
#include <stdexcept>
static void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << std::endl;
        throw std::runtime_error(message);
    }
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &condition, int ms = 15000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (condition())
            return true;
        QThread::msleep(5);
    } while (timer.elapsed() < ms);
    return false;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3)
        return 2;
    const QString helper = QString::fromLocal8Bit(argv[1]),
                  root = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[2]));
    try {
        QUdpSocket stun;
        require(stun.bind(QHostAddress::LocalHost, 38173), "local STUN canary listening");
        QTcpServer proxy;
        require(proxy.listen(QHostAddress::LocalHost, 0), "local remote-DNS canary proxy");
        unsigned namedConnections = 0;
        QObject::connect(&proxy, &QTcpServer::newConnection, &app, [&] {
            while (proxy.hasPendingConnections()) {
                auto *s = proxy.nextPendingConnection();
                QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
                    const auto request = s->readAll();
                    if (request.startsWith("CONNECT destination.streambridge.invalid:443 "))
                        ++namedConnections;
                    s->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n");
                    s->disconnectFromHost();
                });
            }
        });
        sb::BrowserConfig config;
        config.url = "https://fixture.streambridge.invalid/";
        config.width = 256;
        config.height = 128;
        config.proxy.host = "127.0.0.1";
        config.proxy.port = proxy.serverPort();
        config.proxy.protocol = sb::ProxyProtocol::HttpConnect;
        QString error;
        const auto decoded = sb::decodeBrowserConfig(sb::encodeBrowserConfig(config), error);
        require(error.isEmpty() && decoded.url == config.url && decoded.proxy.port == config.proxy.port,
                "encrypted-config payload roundtrip");
        unsigned frames = 0, audio = 0;
        bool transparent = false, partial = false, changed = false, shape = true;
        QByteArray previous;
        double peak = 0;
        sb::BrowserHost host;
        QObject::connect(&host, &sb::BrowserHost::video, &app, [&](QByteArray bytes, int w, int h, quint64) {
            shape &= w == 256 && h == 128;
            ++frames;
            transparent |= static_cast<unsigned char>(bytes[3]) == 0;
            for (qsizetype i = 3; i < bytes.size(); i += 4) {
                const auto a = static_cast<unsigned char>(bytes[i]);
                if (a > 100 && a < 160)
                    partial = true;
            }
            if (!previous.isEmpty() && bytes != previous)
                changed = true;
            previous = bytes;
        });
        QObject::connect(&host, &sb::BrowserHost::audio, &app, [&](QByteArray bytes, int count, quint64) {
            ++audio;
            shape &= bytes.size() == count * 2 * 4;
            for (qsizetype i = 0; i < bytes.size(); i += 4) {
                float x;
                std::memcpy(&x, bytes.constData() + i, 4);
                if (std::isfinite(x))
                    peak = std::max(peak, static_cast<double>(std::abs(x)));
            }
        });
        require(host.start(helper, config, error), "sandboxed synthetic widget helper started");
        require(until([&] {
                    return frames > 20 && audio > 30 && transparent && partial && changed && peak > 0.01 &&
                           namedConnections > 0;
                }),
                "JavaScript animation, alpha, PCM and remote target hostname");
        require(shape, "configured frame dimensions and stereo PCM shape");
        require(!stun.hasPendingDatagrams(), "WebRTC STUN canary received no direct UDP");
        host.stop();
        require(!host.processId(), "active media helper stopped");

        require(obs_startup("en-US", nullptr, nullptr), "libobs startup");
        obs_add_data_path((root + "/data/libobs/").toUtf8().constData());
        obs_audio_info ai{};
        ai.samples_per_sec = 48000;
        ai.speakers = SPEAKERS_STEREO;
        require(obs_reset_audio(&ai), "OBS audio clock");
        const auto graphics = (root + "/bin/64bit/libobs-d3d11.dll").toUtf8();
        obs_video_info vi{};
        vi.graphics_module = graphics.constData();
        vi.fps_num = 30;
        vi.fps_den = 1;
        vi.base_width = 256;
        vi.base_height = 128;
        vi.output_width = 256;
        vi.output_height = 128;
        vi.output_format = VIDEO_FORMAT_BGRA;
        vi.colorspace = VIDEO_CS_709;
        vi.range = VIDEO_RANGE_FULL;
        require(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS, "OBS D3D11 clock");
        obs_source_info info{};
        info.id = "streambridge_media_test";
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
        info.get_name = [](void *) { return "Browser media test"; };
        info.create = [](obs_data_t *, obs_source_t *s) -> void * { return s; };
        info.destroy = [](void *) {};
        obs_register_source(&info);
        auto *source = obs_source_create_private(info.id, "browser-test", nullptr);
        require(source, "real OBS asynchronous source");
        obs_source_set_async_unbuffered(source, true);
        obs_source_set_async_decoupled(source, true);
        obs_set_output_source(0, source);
        std::atomic<unsigned> obsAudio{0};
        obs_source_add_audio_capture_callback(
            source,
            [](void *p, obs_source_t *, const audio_data *a, bool) {
                if (a->frames)
                    ++*static_cast<std::atomic<unsigned> *>(p);
            },
            &obsAudio);
        {
            QThread thread;
            auto *worker = new sb::BrowserWorker(source, helper);
            worker->moveToThread(&thread);
            QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
            thread.start();
            QMetaObject::invokeMethod(
                worker, [worker, config] { worker->configure(config); }, Qt::QueuedConnection);
            const bool media = until([&] {
                return obs_source_get_width(source) == 256 && obs_source_get_height(source) == 128 &&
                       obsAudio > 10;
            });
            QMetaObject::invokeMethod(worker, [worker] { worker->stop(); }, Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
            require(media, "CEF frames and audio delivered through real libobs source");
        }
        obs_set_output_source(0, nullptr);
        obs_source_release(source);
        obs_shutdown();
        std::cout << "PASS: browser media tests complete; fixture is synthetic, not a DonationAlerts "
                     "acceptance test\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
