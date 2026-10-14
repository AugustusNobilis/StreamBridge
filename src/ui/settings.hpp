// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stream/output-manager.hpp"
#include <QDialog>
namespace sb {
class Settings : public QDialog {
  public:
    explicit Settings(OutputManager &manager, QWidget *parent = nullptr);
};
} // namespace sb
