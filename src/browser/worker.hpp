// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/retry.hpp"
#include "host.hpp"
#include <QElapsedTimer>
#include <QTimer>
#include <obs.h>
namespace sb {
class BrowserWorker final : public QObject {
    Q_OBJECT
  public:
    BrowserWorker(obs_source_t *source, QString helper);
    void configure(const BrowserConfig &config);
    void refresh();
    void stop();
    QString state() const { return state_; }
  signals:
    void changed(QString state);

  private:
    void start();
    void failed(const QString &code);
    void setState(const QString &state);
    obs_source_t *source_;
    QString helper_, state_ = "BrowserUnconfigured";
    BrowserConfig config_;
    BrowserHost host_;
    RetryPolicy retry_;
    QTimer reconnect_, deadline_;
    QElapsedTimer stable_;
    bool live_ = false, stopped_ = true;
};
} // namespace sb
