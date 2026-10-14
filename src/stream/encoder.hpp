// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config/config.hpp"
#include <memory>
#include <obs.h>

namespace sb {
struct EncoderPair {
    obs_encoder_t *video = nullptr;
    obs_encoder_t *audio = nullptr;
    ~EncoderPair();
    EncoderPair() = default;
    EncoderPair(const EncoderPair &) = delete;
    EncoderPair &operator=(const EncoderPair &) = delete;
};
std::shared_ptr<EncoderPair> createEncoders(const EncoderProfile &profile, const QString &name,
                                            QString &error);
const char *presetProperty(obs_properties_t *properties);
} // namespace sb
