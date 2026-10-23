// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace terraingen {

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool HasExtension(const std::filesystem::path& path,
                  std::initializer_list<std::string_view> extensions) {
    const std::string extension = Lower(path.extension().string());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](std::string_view candidate) { return extension == Lower(candidate); });
}

std::string FormatNumber(double value, int digits) {
    std::array<char, 40> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*g", digits, value);
    return buffer.data();
}

}  // namespace terraingen
