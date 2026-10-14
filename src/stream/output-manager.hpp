// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config/config.hpp"
#include "core/retry.hpp"
#include "encoder.hpp"
#include "oauth/account.hpp"
#include "security/secret-store.hpp"
#include <QObject>
#include <QTimer>
#include <chrono>
#include <deque>
#include <mutex>

namespace sb {
enum class StreamState { Idle, Connecting, Live, Backoff, Stopping, Failed };
struct StreamStatus {
    StreamState state = StreamState::Idle;
    double kbps = 0;
    int dropped = 0;
    int64_t seconds = 0;
    unsigned reconnects = 0;
    int retrySeconds = 0;
    QString error;
};
class OutputManager : public QObject {
    Q_OBJECT
  public:
    OutputManager(QString directory, QObject *parent = nullptr, QString outputType = "rtmp_output");
    ~OutputManager() override;
    const Config &config() const { return config_; }
    bool configure(const Config &config, QString &error);
    bool busy() const;
    const StreamStatus &status(size_t index) const { return sessions_.at(index).status; }
    void start(size_t index);
    void stop(size_t index);
    void startAll();
    void stopAll();
    void shutdown();
    SecretStore &secrets() { return secrets_; }
    OAuthAccount &account(size_t index) { return *accounts_.at(index); }
    bool applyIngest(size_t index, const QString &server, const QByteArray &key, QString &error);
    QStringList diagnostics() const { return diagnostics_; }
    // Read-only integration instrumentation: returned references are borrowed.
    obs_output_t *output(size_t index) const { return sessions_.at(index).output; }
  signals:
    void changed();

  private:
    using Clock = std::chrono::steady_clock;
    struct Event {
        bool started;
        int code;
    };
    struct Session {
        obs_output_t *output = nullptr;
        obs_service_t *service = nullptr;
        std::shared_ptr<EncoderPair> encoders;
        StreamStatus status;
        RetryPolicy retry;
        bool wanted = false;
        bool pending = false;
        Clock::time_point deadline{}, connected{}, sampleTime{};
        uint64_t bytes = 0;
        std::mutex mutex;
        std::deque<Event> events;
    };
    static void onStart(void *data, calldata_t *);
    static void onStop(void *data, calldata_t *call);
    bool prepare(size_t index);
    void attempt(size_t index);
    void failed(size_t index, int code);
    void release(Session &session);
    void tick();
    void record(size_t index, const QString &message);
    QString directory_;
    QString outputType_;
    SecretStore secrets_;
    std::array<std::unique_ptr<OAuthAccount>, 2> accounts_;
    Config config_;
    std::array<Session, 2> sessions_;
    std::shared_ptr<EncoderPair> common_;
    QTimer timer_;
    bool closing_ = false;
    QStringList diagnostics_;
};
} // namespace sb
