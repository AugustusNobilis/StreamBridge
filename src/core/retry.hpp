// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace sb {
// Equal jitter: [half the capped exponential delay, full delay]. No zero-delay retry.
class RetryPolicy {
  public:
    explicit RetryPolicy(unsigned maximum = 12) : maximum_(maximum) {}
    std::optional<std::chrono::milliseconds> next(uint32_t random);
    void reset() { attempts_ = 0; }
    unsigned attempts() const { return attempts_; }

  private:
    unsigned attempts_ = 0;
    unsigned maximum_;
};
} // namespace sb
