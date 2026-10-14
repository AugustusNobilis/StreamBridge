// SPDX-License-Identifier: GPL-2.0-or-later
#include "retry.hpp"
#include <algorithm>

namespace sb {
std::optional<std::chrono::milliseconds> RetryPolicy::next(uint32_t random) {
    if (attempts_ >= maximum_)
        return std::nullopt;
    const uint32_t ceiling = std::min(60000u, 2000u << std::min(attempts_++, 5u));
    const uint32_t floor = ceiling / 2;
    return std::chrono::milliseconds(floor + random % (ceiling - floor + 1));
}
} // namespace sb
