// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include <QJsonDocument>
#include <QJsonObject>
namespace sb {
QByteArray encodeBrowserConfig(const BrowserConfig &c) {
    return QJsonDocument(QJsonObject{{"version", 1},
                                     {"url", c.url},
                                     {"width", c.width},
                                     {"height", c.height},
                                     {"fps", c.fps},
                                     {"proxyEnabled", c.proxyEnabled},
                                     {"reconnect", c.reconnect},
                                     {"protocol", static_cast<int>(c.proxy.protocol)},
                                     {"host", c.proxy.host},
                                     {"port", c.proxy.port},
                                     {"username", QString::fromUtf8(c.proxy.username)},
                                     {"password", QString::fromUtf8(c.proxy.password)}})
        .toJson(QJsonDocument::Compact);
}
BrowserConfig decodeBrowserConfig(const QByteArray &json, QString &error) {
    BrowserConfig c;
    error.clear();
    if (json.isEmpty())
        return c;
    if (json.size() > 65536) {
        error = "BrowserInvalid";
        return c;
    }
    const auto doc = QJsonDocument::fromJson(json);
    const auto o = doc.object();
    const auto protocol = o.value("protocol").toInt(-1);
    if (!doc.isObject() || o.value("version").toInt() != 1 || protocol < 0 || protocol > 2) {
        error = "BrowserInvalid";
        return c;
    }
    c.url = o.value("url").toString();
    c.width = o.value("width").toInt();
    c.height = o.value("height").toInt();
    c.fps = o.value("fps").toInt();
    c.proxyEnabled = o.value("proxyEnabled").toBool(true);
    c.reconnect = o.value("reconnect").toBool(true);
    c.proxy.protocol = static_cast<ProxyProtocol>(protocol);
    c.proxy.host = o.value("host").toString();
    const int port = o.value("port").toInt();
    if (port < 1 || port > 65535) {
        error = "ProxyInvalid";
        return c;
    }
    c.proxy.port = static_cast<quint16>(port);
    c.proxy.username = o.value("username").toString().toUtf8();
    c.proxy.password = o.value("password").toString().toUtf8();
    error = validateBrowser(c);
    return c;
}
} // namespace sb
