/// @file
/// @brief Reading and writing whole files.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace terraingen {

/// The bytes of a file.
/// @throws std::runtime_error "Cannot read <path>" if the file cannot be opened or read.
std::string ReadFile(const std::filesystem::path& path);

/// Replaces a file's contents with `bytes`.
/// @throws std::runtime_error "Cannot write <path>" if the file cannot be opened or written.
void WriteFile(const std::filesystem::path& path, std::string_view bytes);

}  // namespace terraingen
