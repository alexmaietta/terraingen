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

}  // namespace terraingen::testing
