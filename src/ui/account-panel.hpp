// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stream/output-manager.hpp"
#include <QWidget>
namespace sb {
QWidget *accountPanel(OutputManager &manager, size_t index, QWidget *parent);
}
