// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Terrain reports written as JSON.
#pragma once

#include "terraingen/pipeline/report.h"

#include <filesystem>
#include <string>

namespace terraingen {

/// `report` as JSON text, with NaN written as null and doubles written exactly.
/// @param indent Spaces per level; negative for one line.
std::string ReportToJson(const TerrainReport& report, int indent = 2);

/// Writes `report` to a file as `ReportToJson` gives it, with a final newline.
/// @throws std::runtime_error "Cannot write <path>" if the file cannot be written.
void WriteReport(const std::filesystem::path& path, const TerrainReport& report);

}  // namespace terraingen
