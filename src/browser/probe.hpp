// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host.hpp"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
namespace sb {
// A short-lived worker-thread probe. Only explicit UI actions construct one.
class BrowserProbe final : public QObject {
    Q_OBJECT
  public:
    explicit BrowserProbe(QObject *parent = nullptr);
    void start(const QString &helper, const BrowserConfig &config, bool externalIp);
  signals:
    void result(QString message);

  private:
    void finish(const QString &message);
    BrowserHost browser_;
    ProxyAdapter proxy_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer *deadline_;
    bool finished_ = false;
};
} // namespace sb
