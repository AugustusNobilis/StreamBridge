// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QSslConfiguration>
#include <QTcpServer>

namespace sb {
enum class ProxyProtocol { HttpConnect, HttpsConnect, Socks5 };
struct ProxyConfig {
    ProxyProtocol protocol = ProxyProtocol::Socks5;
    QString host;
    quint16 port = 1080;
    QByteArray username, password;
    // Normal system trust in production. Tests can supply a private fixture CA without changing Windows.
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
};
QString validateProxy(const ProxyConfig &config);
class ProxyAdapter : public QObject {
    Q_OBJECT
  public:
    explicit ProxyAdapter(QObject *parent = nullptr);
    ~ProxyAdapter() override;
    bool start(const ProxyConfig &config, const QByteArray &capability, QString &error);
    void stop();
    quint16 port() const { return server_.serverPort(); }
  signals:
    void failed(QString code);

  private:
    QTcpServer server_;
    ProxyConfig config_;
    QByteArray capability_;
};
} // namespace sb
