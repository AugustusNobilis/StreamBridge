// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stream/output-manager.hpp"
#include <QWidget>
class QLabel;
class QPushButton;
class QComboBox;
namespace sb {
class PlatformHub;
class ViewerBadge;
class Dock : public QWidget {
  public:
    explicit Dock(OutputManager &manager, PlatformHub &hub, QWidget *parent = nullptr);
    void openSettings();

  private:
    void refresh();
    OutputManager &manager_;
    std::array<ViewerBadge *, 2> viewers_{};
    std::array<QLabel *, 2> status_{};
    std::array<QLabel *, 2> metrics_{};
    std::array<QPushButton *, 2> start_{};
    std::array<QPushButton *, 2> stop_{};
    QComboBox *mode_;
};
} // namespace sb
