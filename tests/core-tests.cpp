// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/redact.hpp"
#include "core/retry.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main() {
    sb::RetryPolicy a, b;
    for (unsigned i = 0; i < 12; ++i) {
        auto low = a.next(0);
        auto high = b.next(std::numeric_limits<uint32_t>::max());
        check(low.has_value() && high.has_value(), "retry available within budget");
        check(low->count() >= 1000 && high->count() <= 60000, "delay bounded, nonzero");
        check(high->count() >= low->count(), "jitter lower bound");
    }
    check(!a.next(0), "retry exhaustion is terminal");
    check(!a.next(0), "exhaustion persists");
    a.reset();
    check(a.next(0)->count() == 1000, "new session resets backoff");
    check(b.attempts() == 12, "independent policies");
    check(sb::redact("prefix secret secret", {"secret"}) == "prefix [REDACTED] [REDACTED]",
          "secrets redacted");
    const auto redacted =
        sb::redact("rtmps://host/live/key?token=value Bearer abc123\nhttps://user:pass@host/path");
    check(redacted.find("key") == std::string::npos && redacted.find("abc123") == std::string::npos &&
              redacted.find("pass") == std::string::npos,
          "URL and authorization redaction");
    check(sb::redact("normal status", {""}) == "normal status", "empty secret cannot hang");
    std::cout << "PASS: retry budget, bounds, reset, isolation and redaction\n";
}
