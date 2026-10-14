// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <obs-module.h>
namespace sb {
inline QString text(const char *key) {
    return QString::fromUtf8(obs_module_text(key));
}
} // namespace sb
