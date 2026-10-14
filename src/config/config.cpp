// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

namespace sb {
QString endpointName(size_t index) {
    return index == 0 ? "YouTube" : "Twitch";
}
QString validateServer(const QString &server) {
    const QUrl url(server, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "rtmps" || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        url.hasQuery() || url.hasFragment() || server.contains(QChar('\n')) || server.contains(QChar('\r')) ||
        url.port(443) < 1)
        return "Use an RTMPS URL without credentials, query parameters or fragment.";
    return {};
}
QString validateProfile(const EncoderProfile &p) {
    if (p.id.isEmpty() || p.bitrate < 100 || p.bitrate > 100000 || p.divisor < 1 || p.divisor > 6 ||
        p.width < 0 || p.height < 0 || p.width > 7680 || p.height > 4320 || p.width % 2 || p.height % 2 ||
        ((p.width == 0) != (p.height == 0)) || (p.width && (p.width < 16 || p.height < 16)) ||
        p.keyframe < 1 || p.keyframe > 10 || p.audioTrack < 1 || p.audioTrack > 6 || p.audioBitrate < 64 ||
        p.audioBitrate > 320)
        return "Invalid encoder profile (dimensions must be even; use 0 x 0 for OBS output size).";
    return {};
}
static QJsonObject encode(const EncoderProfile &p) {
    return {{"id", p.id},
            {"bitrate", p.bitrate},
            {"width", p.width},
            {"height", p.height},
            {"divisor", p.divisor},
            {"keyframe", p.keyframe},
            {"rateControl", p.rateControl},
            {"preset", p.preset},
            {"audioBitrate", p.audioBitrate},
            {"audioTrack", p.audioTrack}};
}
static EncoderProfile decode(const QJsonObject &o) {
    EncoderProfile p;
    p.id = o.value("id").toString(p.id);
    p.bitrate = o.value("bitrate").toInt(p.bitrate);
    p.width = o.value("width").toInt(0);
    p.height = o.value("height").toInt(0);
    p.divisor = o.value("divisor").toInt(1);
    p.keyframe = o.value("keyframe").toInt(2);
    p.rateControl = o.value("rateControl").toString("CBR");
    p.preset = o.value("preset").toString();
    p.audioBitrate = o.value("audioBitrate").toInt(160);
    p.audioTrack = o.value("audioTrack").toInt(1);
    return p;
}
Config loadConfig(const QString &directory, QString &error) {
    Config config;
    QFile file(directory + "/settings.json");
    if (!file.exists())
        return config;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        error = "Cannot read StreamBridge settings.";
        return config;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    if (parse.error != QJsonParseError::NoError || object.value("schema").toInt() != 1) {
        error = "Invalid or unsupported settings file. Defaults loaded; original file preserved.";
        return config;
    }
    config.advanced = object.value("advanced").toBool();
    config.common = decode(object.value("common").toObject());
    const auto endpoints = object.value("endpoints").toArray();
    if (endpoints.size() != 2) {
        error = "Invalid endpoints in settings.";
        return Config{};
    }
    for (int i = 0; i < 2; ++i) {
        const auto ep = endpoints[i].toObject();
        config.endpoints[i].server = ep.value("server").toString();
        config.endpoints[i].encoder = decode(ep.value("encoder").toObject());
        error = validateServer(config.endpoints[i].server);
        if (error.isEmpty())
            error = validateProfile(config.endpoints[i].encoder);
        if (!error.isEmpty())
            return Config{};
    }
    error = validateProfile(config.common);
    return error.isEmpty() ? config : Config{};
}
bool saveConfig(const QString &directory, const Config &config, QString &error) {
    QJsonArray endpoints;
    for (const auto &ep : config.endpoints)
        endpoints.append(QJsonObject{{"server", ep.server}, {"encoder", encode(ep.encoder)}});
    const QJsonDocument document(QJsonObject{{"schema", 1},
                                             {"advanced", config.advanced},
                                             {"common", encode(config.common)},
                                             {"endpoints", endpoints}});
    if (!QDir().mkpath(directory)) {
        error = "Cannot create settings directory.";
        return false;
    }
    QSaveFile file(directory + "/settings.json");
    const auto bytes = document.toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot save settings atomically.";
        return false;
    }
    return true;
}
} // namespace sb
