// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <obs-module.h>
namespace sb {
QString text(const char *key);
void initializeLocale(const QString &directory);
void shutdownLocale();
QString language();
bool setLanguage(const QString &language);
} // namespace sb
