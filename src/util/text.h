// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Text helpers: case-insensitive names and file extensions, number formatting, and whole
/// files.
#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace terraingen {

/// `text` with ASCII letters lowercased.
std::string Lower(std::string_view text);

/// Whether `path` ends in one of `extensions`, each with its dot (".tif"), ignoring case.
bool HasExtension(const std::filesystem::path& path,
                  std::initializer_list<std::string_view> extensions);

/// `value` as printf `%g` writes it with `digits` significant digits; 17 reads any double back
/// exactly.
std::string FormatNumber(double value, int digits);

/// The bytes of a file.
/// @throws std::runtime_error "Cannot read <path>" if the file cannot be opened or read.
std::string ReadFile(const std::filesystem::path& path);

/// Replaces a file's contents with `bytes`.
/// @throws std::runtime_error "Cannot write <path>" if the file cannot be opened or written.
void WriteFile(const std::filesystem::path& path, std::string_view bytes);

}  // namespace terraingen
