// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
namespace sb {
std::string redact(std::string input, const std::vector<std::string> &secrets = {});
}
