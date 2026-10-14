// SPDX-License-Identifier: GPL-2.0-or-later
#include "worker.hpp"
#include <QRandomGenerator>
#include <cmath>
#include <cstring>
#include <util/platform.h>
namespace sb {
BrowserWorker::BrowserWorker(obs_source_t *source, QString helper)
    : source_(source), helper_(std::move(helper)), host_(this), reconnect_(this), deadline_(this) {
    reconnect_.setSingleShot(true);
    deadline_.setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, &BrowserWorker::start);
    connect(&deadline_, &QTimer::timeout, this, [this] { failed("BrowserLoadTimeout"); });
    connect(
        &host_, &BrowserHost::failed, this, [this](const QString &code) { failed(code); },
        Qt::QueuedConnection);
    connect(&host_, &BrowserHost::status, this, [this](unsigned code) {
        if (stopped_)
            return;
        if (code == static_cast<unsigned>(browser::Status::Loaded)) {
            live_ = true;
            deadline_.stop();
            stable_.start();
            setState("BrowserLoaded");
        } else if (code == static_cast<unsigned>(browser::Status::LoadFailed) ||
                   code == static_cast<unsigned>(browser::Status::RendererFailed)) {
            // Queue shutdown: never destroy a signal sender while parsing its IPC.
            QTimer::singleShot(0, this, [this] { failed("BrowserLoadFailed"); });
        } else if (code == static_cast<unsigned>(browser::Status::AudioFailed))
            setState("BrowserAudioFailed");
    });
    connect(&host_, &BrowserHost::video, this,
            [this](QByteArray bytes, int width, int height, quint64 timestamp) {
                if (stopped_ || !live_)
                    return;
                // CEF supplies premultiplied BGRA. OBS asynchronous frames use straight
                // color with an alpha channel, including its nonlinear-alpha shader path.
                auto *pixels = reinterpret_cast<unsigned char *>(bytes.data());
                for (qsizetype i = 0; i < bytes.size(); i += 4) {
                    const unsigned a = pixels[i + 3];
                    if (a && a < 255)
                        for (int c = 0; c < 3; ++c)
                            pixels[i + c] = static_cast<unsigned char>(
                                std::min(255u, (pixels[i + c] * 255u + a / 2) / a));
                }
                // OBS copies this frame synchronously; the IPC buffer never escapes this callback.
                obs_source_frame frame{};
                frame.data[0] = reinterpret_cast<uint8_t *>(bytes.data());
                frame.linesize[0] = width * 4;
                frame.width = width;
                frame.height = height;
                frame.format = VIDEO_FORMAT_BGRA;
                frame.full_range = true;
                frame.timestamp = timestamp;
                obs_source_output_video(source_, &frame);
            });
    connect(&host_, &BrowserHost::audio, this, [this](QByteArray bytes, int frames, quint64 timestamp) {
        if (stopped_ || !live_)
            return;
        // Treat the renderer/helper boundary as untrusted, including non-finite PCM.
        for (qsizetype i = 0; i < bytes.size(); i += sizeof(float)) {
            float sample;
            std::memcpy(&sample, bytes.constData() + i, sizeof(sample));
            if (!std::isfinite(sample))
                return;
        }
        obs_source_audio audio{};
        audio.frames = frames;
        audio.samples_per_sec = 48000;
        audio.speakers = SPEAKERS_STEREO;
        audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
        audio.timestamp = timestamp;
        audio.data[0] = reinterpret_cast<const uint8_t *>(bytes.constData());
        audio.data[1] = audio.data[0] + frames * sizeof(float);
        obs_source_output_audio(source_, &audio);
    });
}
void BrowserWorker::setState(const QString &state) {
    state_ = state;
    emit changed(state);
}
void BrowserWorker::configure(const BrowserConfig &config) {
    stop();
    config_ = config;
    retry_.reset();
    stopped_ = false;
    start();
}
void BrowserWorker::start() {
    if (stopped_)
        return;
    live_ = false;
    stable_.invalidate();
    if (config_.url.isEmpty()) {
        setState("BrowserUnconfigured");
        return;
    }
    setState("BrowserConnecting");
    QString error;
    if (!host_.start(helper_, config_, error)) {
        failed(error);
        return;
    }
    deadline_.start(30000);
}
void BrowserWorker::failed(const QString &code) {
    if (stopped_ || reconnect_.isActive())
        return;
    if (stable_.isValid() && stable_.elapsed() >= 60000)
        retry_.reset();
    live_ = false;
    stable_.invalidate();
    deadline_.stop();
    host_.stop();
    obs_source_output_video(source_, nullptr);
    setState(code);
    if (!config_.reconnect || code == "BrowserInvalid" || code == "ProxyInvalid" ||
        code == "BrowserHelperMissing")
        return;
    if (const auto delay = retry_.next(QRandomGenerator::system()->generate())) {
        setState(QString("BrowserRetry (%1/12, %2 s)")
                     .arg(retry_.attempts())
                     .arg(delay->count() / 1000.0, 0, 'f', 1));
        reconnect_.start(static_cast<int>(delay->count()));
    }
}
void BrowserWorker::refresh() {
    retry_.reset();
    reconnect_.stop();
    deadline_.stop();
    host_.stop();
    live_ = false;
    stopped_ = false;
    start();
}
void BrowserWorker::stop() {
    stopped_ = true;
    live_ = false;
    reconnect_.stop();
    deadline_.stop();
    host_.stop();
    obs_source_output_video(source_, nullptr);
}
} // namespace sb
