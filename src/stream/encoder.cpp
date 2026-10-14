// SPDX-License-Identifier: GPL-2.0-or-later
#include "encoder.hpp"
#include <cstring>

namespace sb {
EncoderPair::~EncoderPair() {
    if (video)
        obs_encoder_release(video);
    if (audio)
        obs_encoder_release(audio);
}
const char *presetProperty(obs_properties_t *properties) {
    if (obs_properties_get(properties, "preset2"))
        return "preset2";
    if (obs_properties_get(properties, "preset"))
        return "preset";
    return nullptr;
}
static bool setChoice(obs_data_t *data, obs_properties_t *props, const char *key, const QString &value,
                      QString &error) {
    if (!key || value.isEmpty())
        return true; // Keep the actual encoder default.
    auto *property = obs_properties_get(props, key);
    if (!property || obs_property_get_type(property) != OBS_PROPERTY_LIST ||
        obs_property_list_format(property) != OBS_COMBO_FORMAT_STRING) {
        error = "This encoder does not expose the selected setting.";
        return false;
    }
    const auto bytes = value.toUtf8();
    for (size_t i = 0; i < obs_property_list_item_count(property); ++i) {
        if (!obs_property_list_item_disabled(property, i) &&
            std::strcmp(obs_property_list_item_string(property, i), bytes.constData()) == 0) {
            obs_data_set_string(data, key, bytes.constData());
            return true;
        }
    }
    error = "Selected encoder option is unavailable. Reopen Encoding settings.";
    return false;
}
std::shared_ptr<EncoderPair> createEncoders(const EncoderProfile &p, const QString &name, QString &error) {
    error = validateProfile(p);
    if (!error.isEmpty())
        return {};
    obs_video_info video{};
    if (!obs_get_video_info(&video) || video.colorspace == VIDEO_CS_2100_PQ ||
        video.colorspace == VIDEO_CS_2100_HLG) {
        error = "StreamBridge H.264 currently requires SDR video. Select Rec.709 in OBS Advanced settings.";
        return {};
    }
    const auto id = p.id.toUtf8();
    const char *codec = obs_get_encoder_codec(id.constData());
    if (!codec || std::strcmp(codec, "h264") != 0 ||
        (obs_get_encoder_caps(id.constData()) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))) {
        error = "Selected H.264 encoder is unavailable.";
        return {};
    }
    auto *settings = obs_encoder_defaults(id.constData());
    if (!settings)
        settings = obs_data_create();
    auto *properties = obs_get_encoder_properties(id.constData());
    if (!properties) {
        obs_data_release(settings);
        error = "Encoder properties unavailable.";
        return {};
    }
    auto *bitrate = obs_properties_get(properties, "bitrate");
    if (!bitrate || obs_property_get_type(bitrate) != OBS_PROPERTY_INT) {
        obs_properties_destroy(properties);
        obs_data_release(settings);
        error = "Encoder does not expose a supported bitrate control.";
        return {};
    }
    obs_data_set_int(settings, "bitrate", p.bitrate);
    if (auto *keyframe = obs_properties_get(properties, "keyint_sec");
        keyframe && obs_property_get_type(keyframe) == OBS_PROPERTY_INT)
        obs_data_set_int(settings, "keyint_sec", p.keyframe);
    const bool optionsOk = setChoice(settings, properties, "rate_control", p.rateControl, error) &&
                           setChoice(settings, properties, presetProperty(properties), p.preset, error);
    obs_properties_destroy(properties);
    if (!optionsOk) {
        obs_data_release(settings);
        return {};
    }
    auto pair = std::make_shared<EncoderPair>();
    pair->video =
        obs_video_encoder_create(id.constData(), (name + " video").toUtf8().constData(), settings, nullptr);
    obs_data_release(settings);
    if (!pair->video) {
        error = "Cannot create video encoder.";
        return {};
    }
    obs_encoder_set_video(pair->video, obs_get_video());
    obs_encoder_set_scaled_size(pair->video, static_cast<uint32_t>(p.width), static_cast<uint32_t>(p.height));
    obs_encoder_set_preferred_video_format(pair->video, VIDEO_FORMAT_NV12);
    if (!obs_encoder_set_frame_rate_divisor(pair->video, static_cast<uint32_t>(p.divisor))) {
        error = "OBS rejected the frame-rate divisor.";
        return {};
    }
    // The OBS FFmpeg AAC encoder is distributed with the host. Do not silently substitute a different codec.
    settings = obs_encoder_defaults("ffmpeg_aac");
    if (!settings)
        settings = obs_data_create();
    obs_data_set_int(settings, "bitrate", p.audioBitrate);
    pair->audio = obs_audio_encoder_create("ffmpeg_aac", (name + " audio").toUtf8().constData(), settings,
                                           static_cast<size_t>(p.audioTrack - 1), nullptr);
    obs_data_release(settings);
    if (!pair->audio) {
        error = "OBS FFmpeg AAC encoder is unavailable.";
        return {};
    }
    obs_encoder_set_audio(pair->audio, obs_get_audio());
    return pair;
}
} // namespace sb
