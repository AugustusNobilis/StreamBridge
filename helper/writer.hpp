// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "browser/protocol.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>
namespace sb::browser {
uint64_t timestamp();
class Writer {
  public:
    Writer();
    ~Writer();
    void push(Header header, const void *data = nullptr);
    void status(Status code);

  private:
    struct Packet {
        Header header;
        std::vector<uint8_t> data;
    };
    void run();
    HANDLE pipe_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Packet> packets_;
    bool stopped_ = false;
    std::thread thread_;
};
} // namespace sb::browser
