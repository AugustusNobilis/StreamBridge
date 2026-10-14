// SPDX-License-Identifier: GPL-2.0-or-later
#include "writer.hpp"
#include <algorithm>
#include <cstring>
namespace sb::browser {
uint64_t timestamp() {
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return (value.QuadPart / frequency.QuadPart) * 1000000000ULL +
           ((value.QuadPart % frequency.QuadPart) * 1000000000ULL) / frequency.QuadPart;
}
Writer::Writer() : pipe_(GetStdHandle(STD_OUTPUT_HANDLE)), thread_([this] { run(); }) {
}
Writer::~Writer() {
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    ready_.notify_all();
    do {
        CancelSynchronousIo(thread_.native_handle());
    } while (WaitForSingleObject(thread_.native_handle(), 50) == WAIT_TIMEOUT);
    thread_.join();
}
void Writer::push(Header header, const void *data) {
    if (header.bytes > maxPayload || (header.bytes && !data))
        return;
    header.timestamp = timestamp();
    Packet packet{header, std::vector<uint8_t>(header.bytes)};
    if (data)
        std::memcpy(packet.data.data(), data, header.bytes);
    std::lock_guard lock(mutex_);
    if (stopped_)
        return;
    if (header.type == PacketType::Video)
        std::erase_if(packets_, [](const Packet &p) { return p.header.type == PacketType::Video; });
    if (packets_.size() >= 64)
        packets_.pop_front();
    packets_.push_back(std::move(packet));
    ready_.notify_one();
}
void Writer::status(Status code) {
    Header header;
    header.sampleRateOrCode = static_cast<uint32_t>(code);
    push(header);
}
static bool writeAll(HANDLE pipe, const void *buffer, size_t size) {
    auto *bytes = static_cast<const uint8_t *>(buffer);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(pipe, bytes, static_cast<DWORD>(size), &written, nullptr) || written == 0)
            return false;
        size -= written;
        bytes += written;
    }
    return true;
}
void Writer::run() {
    for (;;) {
        Packet packet;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopped_ || !packets_.empty(); });
            if (stopped_)
                return;
            packet = std::move(packets_.front());
            packets_.pop_front();
        }
        if (!writeAll(pipe_, &packet.header, sizeof(Header)) ||
            !writeAll(pipe_, packet.data.data(), packet.data.size())) {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            return;
        }
    }
}
} // namespace sb::browser
