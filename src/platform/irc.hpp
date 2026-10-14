// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QMap>
#include <QStringList>
namespace sb {
struct IrcLine {
    QMap<QString, QString> tags;
    QString prefix, command;
    QStringList parameters;
};
IrcLine parseIrc(const QByteArray &line);
} // namespace sb
