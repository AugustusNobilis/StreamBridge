// SPDX-License-Identifier: GPL-2.0-or-later
#include "irc.hpp"
namespace sb {
static QString unescape(const QString &value) {
    QString out;
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            out += value[i];
            continue;
        }
        if (++i == value.size())
            break;
        switch (value[i].unicode()) {
        case 's':
            out += ' ';
            break;
        case ':':
            out += ';';
            break;
        case 'r':
            out += '\r';
            break;
        case 'n':
            out += '\n';
            break;
        default:
            out += value[i];
            break;
        }
    }
    return out;
}
IrcLine parseIrc(const QByteArray &bytes) {
    IrcLine result;
    if (bytes.size() > 16384)
        return result;
    auto line = QString::fromUtf8(bytes);
    if (line.endsWith("\r\n"))
        line.chop(2);
    if (line.contains('\r') || line.contains('\n') || line.contains(QChar(0)))
        return result;
    auto take = [&line] {
        const auto end = line.indexOf(' ');
        auto token = end < 0 ? line : line.left(end);
        line = end < 0 ? QString() : line.mid(end + 1);
        return token;
    };
    if (line.startsWith('@')) {
        const auto tags = take().mid(1).split(';');
        for (const auto &tag : tags) {
            const auto eq = tag.indexOf('=');
            result.tags.insert(eq < 0 ? tag : tag.left(eq), eq < 0 ? QString() : unescape(tag.mid(eq + 1)));
        }
    }
    if (line.startsWith(':'))
        result.prefix = take().mid(1);
    result.command = take();
    while (!line.isEmpty() && result.parameters.size() < 16) {
        if (line.startsWith(':')) {
            result.parameters.append(line.mid(1));
            break;
        }
        auto param = take();
        if (!param.isEmpty())
            result.parameters.append(param);
    }
    return result;
}
} // namespace sb
