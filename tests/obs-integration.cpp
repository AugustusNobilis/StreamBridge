// SPDX-License-Identifier: GPL-2.0-or-later
// Real libobs encoders and output lifecycle, instrumented in-process packet sinks (NOT an RTMPS test).
#include "stream/output-manager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <functional>
#include <iostream>
#include <obs-module.h>
#include <util/platform.h>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &predicate, int timeout = 10000) {
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
struct Sink {
    obs_output_t *output;
    std::atomic<uint64_t> bytes{0}, video{0}, audio{0};
};
static void registerSink() {
    obs_output_info info{};
    info.id = "streambridge_test_sink";
    info.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED;
    info.get_name = [](void *) { return "StreamBridge test sink"; };
    info.create = [](obs_data_t *, obs_output_t *output) -> void * { return new Sink{output}; };
    info.destroy = [](void *data) { delete static_cast<Sink *>(data); };
    info.start = [](void *data) {
        auto *s = static_cast<Sink *>(data);
        if (!obs_output_can_begin_data_capture(s->output, 0) || !obs_output_initialize_encoders(s->output, 0))
            return false;
        return obs_output_begin_data_capture(s->output, 0);
    };
    info.stop = [](void *data, uint64_t) { obs_output_end_data_capture(static_cast<Sink *>(data)->output); };
    info.encoded_packet = [](void *data, encoder_packet *packet) {
        if (!packet)
            return;
        auto *s = static_cast<Sink *>(data);
        s->bytes += packet->size;
        if (packet->type == OBS_ENCODER_VIDEO)
            ++s->video;
        else
            ++s->audio;
    };
    info.get_total_bytes = [](void *data) { return static_cast<Sink *>(data)->bytes.load(); };
    info.encoded_video_codecs = "h264";
    info.encoded_audio_codecs = "aac";
    obs_register_output(&info);
}
static void loadModule(const QString &root, const char *id) {
    const auto binary = (root + "/obs-plugins/64bit/" + id + ".dll").toUtf8();
    const auto data = (root + "/data/obs-plugins/" + id).toUtf8();
    obs_module_t *module = nullptr;
    check(obs_open_module(&module, binary.constData(), data.constData()) == MODULE_SUCCESS &&
              obs_init_module(module),
          id);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: obs-integration <OBS root>\n";
        return 2;
    }
    const QString root = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir temp;
    check(obs_startup("en-US", temp.path().toUtf8().constData(), nullptr), "libobs startup");
    // OBS 32's legacy search path is concatenated with the filename; retain the trailing slash.
    obs_add_data_path((root + "/data/libobs/").toUtf8().constData());
    obs_audio_info audio{};
    audio.samples_per_sec = 48000;
    audio.speakers = SPEAKERS_STEREO;
    check(obs_reset_audio(&audio), "audio clock");
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
    check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "D3D11 video clock");
    loadModule(root, "obs-x264");
    loadModule(root, "obs-ffmpeg");
    loadModule(root, "rtmp-services");
    registerSink();
    {
        sb::OutputManager manager(temp.path(), nullptr, "streambridge_test_sink");
        QString error;
        check(manager.secrets().put("YouTube/stream-key", "local-test-only", error), "first test credential");
        check(manager.secrets().put("Twitch/stream-key", "local-test-only", error), "second test credential");
        auto config = manager.config();
        config.common.bitrate = 500;
        check(manager.configure(config, error), "common config");
        manager.startAll();
        check(until([&] {
                  return manager.status(0).state == sb::StreamState::Live &&
                         manager.status(1).state == sb::StreamState::Live;
              }),
              "two independent outputs live");
        auto *first = manager.output(0);
        auto *second = manager.output(1);
        check(obs_output_get_video_encoder(first) == obs_output_get_video_encoder(second),
              "same video encoder object");
        check(obs_output_get_audio_encoder(first, 0) == obs_output_get_audio_encoder(second, 0),
              "same AAC encoder object");
        auto *s0 = static_cast<Sink *>(obs_obj_get_data(first));
        auto *s1 = static_cast<Sink *>(obs_obj_get_data(second));
        check(until([&] { return s0->video > 30 && s1->video > 30 && s0->audio > 10 && s1->audio > 10; }),
              "real encoded audio/video packets on both outputs");
        check(!manager.configure(config, error), "active mode switch rejected");
        manager.stop(1);
        check(until([&] { return manager.status(1).state == sb::StreamState::Idle; }), "stop one output");
        const auto before = s0->video.load();
        check(until([&] { return s0->video > before + 15; }), "other output keeps encoding");
        manager.start(1);
        check(until([&] { return manager.status(1).state == sb::StreamState::Live; }),
              "join existing common encoder");
        obs_output_signal_stop(manager.output(1), OBS_OUTPUT_DISCONNECTED);
        check(until([&] { return manager.status(1).state == sb::StreamState::Backoff; }),
              "failure enters backoff");
        check(until([&] {
                  return manager.status(1).state == sb::StreamState::Live &&
                         manager.status(1).reconnects == 1;
              }),
              "independent reconnect");
        check(manager.status(0).state == sb::StreamState::Live && manager.status(0).reconnects == 0,
              "peer not restarted");
        manager.stopAll();
        check(until([&] { return !manager.busy(); }), "stop all");
        config.advanced = true;
        config.endpoints[0].encoder.bitrate = 700;
        config.endpoints[1].encoder.bitrate = 300;
        config.endpoints[1].encoder.width = 320;
        config.endpoints[1].encoder.height = 180;
        config.endpoints[1].encoder.divisor = 2;
        check(manager.configure(config, error), "advanced config");
        manager.startAll();
        check(until([&] {
                  return manager.status(0).state == sb::StreamState::Live &&
                         manager.status(1).state == sb::StreamState::Live;
              }),
              "advanced outputs live");
        first = manager.output(0);
        second = manager.output(1);
        auto *v0 = obs_output_get_video_encoder(first);
        auto *v1 = obs_output_get_video_encoder(second);
        check(v0 != v1, "independent encoder objects");
        check(obs_encoder_get_width(v1) == 320 && obs_encoder_get_height(v1) == 180,
              "scaled resolution applied");
        check(obs_encoder_get_frame_rate_divisor(v1) == 2, "FPS divisor applied");
        auto *settings = obs_encoder_get_settings(v1);
        check(obs_data_get_int(settings, "bitrate") == 300, "independent bitrate applied");
        obs_data_release(settings);
        s0 = static_cast<Sink *>(obs_obj_get_data(first));
        s1 = static_cast<Sink *>(obs_obj_get_data(second));
        check(until([&] { return s0->video > 100 && s1->video > 45; }),
              "advanced frames encoded at both rates");
        manager.shutdown();
        check(!manager.busy(), "shutdown during live outputs");
    }
    obs_shutdown();
    std::cout << "PASS: libobs integration complete (no RTMPS network or OBS frontend in this test)\n";
}
