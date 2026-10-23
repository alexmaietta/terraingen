// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Paths for files that tests write.
#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace terraingen::testing {

/// @brief A path in the test framework's temporary directory.
/// @param name The file name.
/// @return `name` inside `::testing::TempDir()`.
inline std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

/// An empty directory `name` in the test framework's temporary directory, emptied if it exists.
inline std::filesystem::path TempDirectory(const std::string& name) {
    const std::filesystem::path directory = TempPath(name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

}  // namespace terraingen::testing
