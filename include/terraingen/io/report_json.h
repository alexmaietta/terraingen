// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Terrain reports written as JSON.
#pragma once

#include "terraingen/pipeline/report.h"

#include <string>

namespace terraingen {

/// `report` as JSON text, with NaN written as null and doubles written exactly.
/// @param indent Spaces per level; negative for one line.
std::string ReportToJson(const TerrainReport& report, int indent = 2);

}  // namespace terraingen
