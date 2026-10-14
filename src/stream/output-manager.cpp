// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-manager.hpp"
#include <QDateTime>
#include <QRandomGenerator>
#include <algorithm>
#include <obs-module.h>

namespace sb {
static QString errorForCode(int code) {
    switch (code) {
    case OBS_OUTPUT_BAD_PATH:
        return "RTMPS server rejected the address or stream key.";
    case OBS_OUTPUT_CONNECT_FAILED:
        return "RTMPS connection failed (network, TLS or server).";
    case OBS_OUTPUT_INVALID_STREAM:
        return "Server rejected this stream. Check the stream key.";
    case OBS_OUTPUT_DISCONNECTED:
        return "Network connection lost.";
    case OBS_OUTPUT_ENCODE_ERROR:
        return "Encoder failed. Check hardware availability and profile.";
    case OBS_OUTPUT_UNSUPPORTED:
        return "Output configuration is unsupported.";
    default:
        return "Output stopped with error code " + QString::number(code) + ".";
    }
}
OutputManager::OutputManager(QString directory, QObject *parent, QString outputType)
    : QObject(parent), directory_(std::move(directory)), outputType_(std::move(outputType)),
      secrets_(directory_) {
    QString error;
    config_ = loadConfig(directory_, error);
    if (!error.isEmpty())
        diagnostics_.append(error);
    accounts_[0] = std::make_unique<OAuthAccount>(Provider::YouTube, secrets_);
    accounts_[1] = std::make_unique<OAuthAccount>(Provider::Twitch, secrets_);
    for (auto &account : accounts_)
        QTimer::singleShot(0, account.get(), &OAuthAccount::resume);
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &OutputManager::tick);
    timer_.start();
}
OutputManager::~OutputManager() {
    shutdown();
}
bool OutputManager::applyIngest(size_t index, const QString &server, const QByteArray &key, QString &error) {
    if (busy()) {
        error = "Stop both StreamBridge outputs before changing credentials.";
        return false;
    }
    error = validateServer(server);
    if (!error.isEmpty() || index >= 2 || key.isEmpty() || key.size() > 8192)
        return false;
    const auto previous = config_;
    auto updated = config_;
    updated.endpoints[index].server = server;
    // Persist config first; restore it if the encrypted key write fails.
    if (!configure(updated, error))
        return false;
    if (secrets_.put(endpointName(index) + "/stream-key", key, error))
        return true;
    QString rollbackError;
    if (!configure(previous, rollbackError))
        error += " Settings rollback failed; check the server before starting.";
    return false;
}
bool OutputManager::busy() const {
    for (const auto &s : sessions_)
        if (s.wanted || s.pending || (s.output && obs_output_active(s.output)))
            return true;
    return false;
}
bool OutputManager::configure(const Config &config, QString &error) {
    if (busy()) {
        error = "Stop both StreamBridge outputs before changing settings.";
        return false;
    }
    error = validateProfile(config.common);
    for (const auto &ep : config.endpoints) {
        if (!error.isEmpty())
            break;
        error = validateServer(ep.server);
        if (error.isEmpty())
            error = validateProfile(ep.encoder);
    }
    if (!error.isEmpty() || !saveConfig(directory_, config, error))
        return false;
    for (auto &s : sessions_)
        release(s);
    common_.reset();
    config_ = config;
    emit changed();
    return true;
}
void OutputManager::onStart(void *data, calldata_t *) {
    auto &s = *static_cast<Session *>(data);
    std::lock_guard lock(s.mutex);
    s.events.push_back({true, 0});
}
void OutputManager::onStop(void *data, calldata_t *call) {
    auto &s = *static_cast<Session *>(data);
    std::lock_guard lock(s.mutex);
    // Do not copy libobs last_error: servers may reflect credentials in errors.
    s.events.push_back({false, static_cast<int>(calldata_int(call, "code"))});
}
void OutputManager::record(size_t index, const QString &message) {
    const QString line =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + endpointName(index) + ": " + message;
    diagnostics_.append(line);
    if (diagnostics_.size() > 200)
        diagnostics_.removeFirst();
    blog(LOG_INFO, "[StreamBridge] %s", line.toUtf8().constData());
}
bool OutputManager::prepare(size_t index) {
    auto &s = sessions_[index];
    if (s.output)
        return true;
    QString error = validateServer(config_.endpoints[index].server);
    if (!error.isEmpty()) {
        s.status.error = error;
        return false;
    }
    auto key = secrets_.get(endpointName(index) + "/stream-key", error);
    if (!error.isEmpty() || key.isEmpty()) {
        s.status.error = error.isEmpty() ? "Save a stream key in Settings first." : error;
        return false;
    }
    // A stream key is kept solely in the encrypted vault and libobs's live service settings.
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "server", config_.endpoints[index].server.toUtf8().constData());
    obs_data_set_string(settings, "key", key.constData());
    key.fill('\0');
    obs_data_set_bool(settings, "use_auth", false);
    const auto name = ("StreamBridge " + endpointName(index)).toUtf8();
    s.service = obs_service_create("rtmp_custom", name.constData(), settings, nullptr);
    obs_data_release(settings);
    if (!s.service) {
        s.status.error = "OBS RTMP service is unavailable.";
        return false;
    }
    if (config_.advanced) {
        s.encoders = createEncoders(config_.endpoints[index].encoder, QString::fromUtf8(name), error);
    } else {
        if (!common_)
            common_ = createEncoders(config_.common, "StreamBridge Common", error);
        s.encoders = common_;
    }
    if (!s.encoders) {
        s.status.error = error;
        release(s);
        return false;
    }
    s.output = obs_output_create(outputType_.toUtf8().constData(), name.constData(), nullptr, nullptr);
    if (!s.output) {
        s.status.error = "OBS RTMP output is unavailable.";
        release(s);
        return false;
    }
    obs_output_set_service(s.output, s.service);
    obs_output_set_video_encoder(s.output, s.encoders->video);
    obs_output_set_audio_encoder(s.output, s.encoders->audio, 0);
    // One retry controller only; libobs's built-in reconnect is disabled for these outputs.
    obs_output_set_reconnect_settings(s.output, 0, 1);
    auto *handler = obs_output_get_signal_handler(s.output);
    signal_handler_connect(handler, "start", onStart, &s);
    signal_handler_connect(handler, "stop", onStop, &s);
    return true;
}
void OutputManager::start(size_t index) {
    if (index >= sessions_.size() || closing_)
        return;
    auto &s = sessions_[index];
    if (s.wanted || s.pending || (s.output && obs_output_active(s.output)))
        return;
    // Retire stale encoders after the previous run, while preserving a live common encoder.
    release(s);
    if (!busy())
        common_.reset();
    s.status = {};
    s.retry.reset();
    if (!prepare(index)) {
        s.status.state = StreamState::Failed;
        record(index, s.status.error);
    } else {
        s.wanted = true;
        attempt(index);
    }
    emit changed();
}
void OutputManager::attempt(size_t index) {
    auto &s = sessions_[index];
    s.status.state = StreamState::Connecting;
    s.pending = true;
    s.bytes = 0;
    s.sampleTime = Clock::now();
    record(index, "Connecting");
    if (!obs_output_start(s.output)) {
        s.pending = false;
        // Synchronous failure means encoder/setup failure, not a network retry.
        s.wanted = false;
        s.status.state = StreamState::Failed;
        s.status.error = "OBS could not start this output. Check encoder settings and the OBS log.";
        record(index, s.status.error);
    }
}
void OutputManager::failed(size_t index, int code) {
    auto &s = sessions_[index];
    s.status.kbps = 0;
    s.status.error = errorForCode(code);
    record(index, s.status.error);
    const bool transient =
        code == OBS_OUTPUT_DISCONNECTED || code == OBS_OUTPUT_CONNECT_FAILED || code == OBS_OUTPUT_ERROR;
    const auto delay = transient ? s.retry.next(QRandomGenerator::global()->generate()) : std::nullopt;
    if (!delay) {
        s.wanted = false;
        s.status.state = StreamState::Failed;
        if (transient)
            s.status.error += " Retry limit reached; press Start to retry.";
        return;
    }
    s.status.state = StreamState::Backoff;
    s.deadline = Clock::now() + *delay;
    s.status.retrySeconds = static_cast<int>((delay->count() + 999) / 1000);
}
void OutputManager::stop(size_t index) {
    if (index >= sessions_.size())
        return;
    auto &s = sessions_[index];
    s.wanted = false;
    if (s.output && (s.pending || obs_output_active(s.output))) {
        s.status.state = StreamState::Stopping;
        // Also cancels an in-progress RTMPS connection.
        obs_output_force_stop(s.output);
    } else {
        s.status.state = StreamState::Idle;
        s.status.kbps = 0;
    }
    emit changed();
}
void OutputManager::startAll() {
    start(0);
    start(1);
}
void OutputManager::stopAll() {
    stop(0);
    stop(1);
}
void OutputManager::tick() {
    const auto now = Clock::now();
    for (size_t index = 0; index < sessions_.size(); ++index) {
        auto &s = sessions_[index];
        std::deque<Event> events;
        {
            std::lock_guard lock(s.mutex);
            events.swap(s.events);
        }
        for (const auto &event : events) {
            if (event.started) {
                if (!s.wanted) {
                    obs_output_force_stop(s.output);
                    continue;
                }
                s.status.state = StreamState::Live;
                s.status.error.clear();
                s.connected = now;
                s.sampleTime = now;
                s.bytes = obs_output_get_total_bytes(s.output);
                record(index, "Live");
            } else {
                s.pending = false;
                if (s.wanted)
                    failed(index, event.code);
                else {
                    s.status.state = StreamState::Idle;
                    s.status.kbps = 0;
                    record(index, "Stopped");
                }
            }
        }
        if (s.status.state == StreamState::Backoff && s.wanted) {
            s.status.retrySeconds = std::max(
                0,
                static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(s.deadline - now).count() + 999) /
                    1000);
            if (now >= s.deadline && !obs_output_active(s.output)) {
                ++s.status.reconnects;
                attempt(index);
            }
        }
        if (s.status.state == StreamState::Live) {
            s.status.seconds = std::chrono::duration_cast<std::chrono::seconds>(now - s.connected).count();
            // Avoid an endless rapid loop when a server repeatedly connects and immediately drops.
            if (s.status.seconds >= 60)
                s.retry.reset();
            const auto bytes = obs_output_get_total_bytes(s.output);
            const auto elapsed = std::chrono::duration<double>(now - s.sampleTime).count();
            s.status.kbps = elapsed > 0 && bytes >= s.bytes ? (bytes - s.bytes) * 8.0 / elapsed / 1000.0 : 0;
            s.bytes = bytes;
            s.sampleTime = now;
            s.status.dropped = obs_output_get_frames_dropped(s.output);
        }
    }
    emit changed();
}
void OutputManager::release(Session &s) {
    if (s.output) {
        auto *handler = obs_output_get_signal_handler(s.output);
        signal_handler_disconnect(handler, "start", onStart, &s);
        signal_handler_disconnect(handler, "stop", onStop, &s);
        obs_output_force_stop(s.output);
        obs_output_release(s.output);
        s.output = nullptr;
    }
    if (s.service) {
        obs_service_release(s.service);
        s.service = nullptr;
    }
    s.encoders.reset();
    s.pending = false;
    std::lock_guard lock(s.mutex);
    s.events.clear();
}
void OutputManager::shutdown() {
    if (closing_)
        return;
    closing_ = true;
    for (auto &account : accounts_)
        account->cancel();
    timer_.stop();
    for (auto &s : sessions_) {
        s.wanted = false;
        release(s);
    }
    common_.reset();
}
} // namespace sb
