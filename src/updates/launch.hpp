// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "checker.hpp"
namespace sb {
// Launch in wait-for-OBS mode. Optional helper restarts only on success; the caller closes OBS.
bool launchUpdate(const QString &file, const Release &release, QString &error,
                  QString *restartCancellation = nullptr);
void cancelUpdateRestart(const QString &event);
bool updateInstallerRunning(const QString &file);
} // namespace sb
