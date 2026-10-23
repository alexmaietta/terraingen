// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/files.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace terraingen {

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
