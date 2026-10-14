// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace sb::browser {
constexpr uint32_t magic = 0x31504253; // SBP1, little-endian x64 Windows.
enum class PacketType : uint32_t { Video = 1, Audio = 2, Status = 3 };
struct Header {
    uint32_t signature = magic;
    PacketType type = PacketType::Status;
    uint32_t bytes = 0;
    uint32_t widthOrFrames = 0;
    uint32_t heightOrChannels = 0;
    uint32_t sampleRateOrCode = 0;
    uint64_t timestamp = 0;
};
static_assert(sizeof(Header) == 32);
constexpr uint32_t maxWidth = 1920, maxHeight = 1080, maxPayload = maxWidth * maxHeight * 4;
enum class Status : uint32_t { Ready = 1, Loaded = 2, LoadFailed = 3, AudioFailed = 4, RendererFailed = 5 };
} // namespace sb::browser
