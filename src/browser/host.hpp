// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include "proxy/adapter.hpp"
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

namespace sb {
struct BrowserConfig {
    QString url;
    int width = 800, height = 600, fps = 30;
    bool proxyEnabled = true;
    bool reconnect = true;
    ProxyConfig proxy;
};
QString validateBrowser(const BrowserConfig &config);
// Own on a worker thread when used by an OBS source. No global/system proxy state is touched.
class BrowserHost final : public QObject {
    Q_OBJECT
  public:
    explicit BrowserHost(QObject *parent = nullptr);
    ~BrowserHost() override;
    bool start(const QString &helperPath, const BrowserConfig &config, QString &error);
    void refresh();
    void stop();
    qint64 processId() const { return process_.processId(); }
    // Development probes only: production callers keep stderr discarded (it may contain renderer
    // diagnostics).
    void setDiagnosticFile(const QString &path) { process_.setStandardErrorFile(path); }
  signals:
    void video(QByteArray bgra, int width, int height, quint64 timestamp);
    void audio(QByteArray planar, int frames, quint64 timestamp);
    void status(unsigned code);
    void networkWarning(QString code);
    void failed(QString code);

  private:
    void read();
    void closeJob();
    ProxyAdapter adapter_;
    QProcess process_;
    std::unique_ptr<QTemporaryDir> directory_;
    QByteArray buffer_;
    BrowserConfig config_;
    void *job_ = nullptr;
    bool stopping_ = false;
};
} // namespace sb
