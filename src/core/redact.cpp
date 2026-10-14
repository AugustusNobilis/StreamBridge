// SPDX-License-Identifier: GPL-2.0-or-later
#include "redact.hpp"
#include <algorithm>
#include <regex>
namespace sb {
std::string redact(std::string input, const std::vector<std::string> &secrets) {
    auto sorted = secrets;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
    for (const auto &secret : sorted) {
        if (secret.empty())
            continue;
        size_t pos = 0;
        while ((pos = input.find(secret, pos)) != std::string::npos) {
            input.replace(pos, secret.size(), "[REDACTED]");
            pos += 10;
        }
    }
    // Diagnostic exports never need complete URLs or authorization headers.
    input = std::regex_replace(input, std::regex(R"((https?|rtmps?|socks5)://[^\s"<>]+)", std::regex::icase),
                               "[URL REDACTED]");
    input = std::regex_replace(input, std::regex(R"((Bearer|OAuth|Basic)\s+[^\s",]+)", std::regex::icase),
                               "[AUTH REDACTED]");
    return input;
}
} // namespace sb
