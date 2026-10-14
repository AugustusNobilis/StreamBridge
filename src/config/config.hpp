// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <array>

namespace sb {
struct EncoderProfile {
    QString id = "obs_x264";
    int bitrate = 6000;
    int width = 0;
    int height = 0;
    int divisor = 1;
    int keyframe = 2;
    QString rateControl = "CBR";
    QString preset;
    int audioBitrate = 160;
    int audioTrack = 1;
};
struct EndpointConfig {
    QString server;
    EncoderProfile encoder;
};
struct Config {
    bool advanced = false;
    EncoderProfile common;
    std::array<EndpointConfig, 2> endpoints{
        {{"rtmps://a.rtmps.youtube.com:443/live2"}, {"rtmps://live.twitch.tv:443/app"}}};
};
QString endpointName(size_t index);
QString validateProfile(const EncoderProfile &profile);
QString validateServer(const QString &server);
Config loadConfig(const QString &directory, QString &error);
bool saveConfig(const QString &directory, const Config &config, QString &error);
} // namespace sb
