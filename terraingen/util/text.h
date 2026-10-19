/// @file
/// @brief Case-insensitive helpers for names and file extensions.
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

}  // namespace terraingen
