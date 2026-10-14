// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host.hpp"
namespace sb {
QByteArray encodeBrowserConfig(const BrowserConfig &config);
BrowserConfig decodeBrowserConfig(const QByteArray &json, QString &error);
} // namespace sb
