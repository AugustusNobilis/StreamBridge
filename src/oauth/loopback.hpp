// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
namespace sb {
QByteArray randomUrlToken();
QByteArray pkceChallenge(const QByteArray &verifier);
class OAuthLoopback : public QObject {
    Q_OBJECT
  public:
    explicit OAuthLoopback(QObject *parent);
    QUrl start(const QByteArray &state);
    void stop();
  signals:
    void completed(QString code, bool denied);
    void expired();

  private:
    QTcpServer listener_;
    QTimer expiry_;
    QByteArray state_;
};
} // namespace sb
