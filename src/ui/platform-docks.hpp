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
    void setCount(qint64 count);
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    qint64 count_ = -1;
};
QWidget *createChatDock(PlatformHub &hub);
QWidget *createInfoDock(PlatformHub &hub, const QString &directory);
} // namespace sb
