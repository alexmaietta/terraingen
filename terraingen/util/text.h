/// @file
/// @brief Text helpers: case-insensitive names and file extensions, and number formatting.
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

}  // namespace terraingen
