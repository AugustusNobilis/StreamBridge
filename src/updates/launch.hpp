// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "checker.hpp"
namespace sb {
// Launch the verified update in wait-for-OBS mode; never closes OBS or stops a stream.
bool launchUpdate(const QString &file, const Release &release, QString &error);
bool updateInstallerRunning(const QString &file);
} // namespace sb
