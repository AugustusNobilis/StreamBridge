// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "platform/hub.hpp"
#include <QIcon>
#include <QWidget>
namespace sb {
QIcon platformIcon(int platform);
class ViewerBadge : public QWidget {
  public:
    explicit ViewerBadge(QWidget *parent = nullptr);
    void setCount(qint64 count, bool unreported = false);
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    qint64 count_ = -1;
    bool unreported_ = false;
    QString displayCount() const;
};
QByteArray chatNotificationWave();
QString streamDuration(qint64 seconds);
QWidget *createChatDock(PlatformHub &hub, const QString &directory, std::function<void()> notify = {});
QWidget *createYouTubeControls(PlatformHub &hub, QWidget *parent);
} // namespace sb
