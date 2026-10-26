// SPDX-License-Identifier: BSD-3-Clause

#include "util/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

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

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return bytes;
}

void WriteFile(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

}  // namespace terraingen
