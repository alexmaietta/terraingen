// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Checking a configuration before building it: everything parsing cannot catch.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/raster/grid.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace terraingen {

/// What a valid configuration generates, as `ValidateConfig` finds it.
struct ConfigSummary {
    /// The source's grid; none without a source or for one without bounds, such as synthetic
    /// noise.
    std::optional<RasterGrid> source;
    /// The grid each output covers, in order; none for a body mesh or a report.
    std::vector<std::optional<RasterGrid>> outputGrids;
    /// Craters the top-level crater layers are expected to generate over the output grid with the
    /// most; unset when none generate or no output covers a grid.
    std::optional<double> expectedCraters;
};

/// Checks everything parsing cannot, building the terrain to check it, and returns what it found.
/// @param directory Folder relative input paths resolve against; empty for the folder the
/// configuration was read from (`TerrainGenConfig::Directory`).
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainGenConfig& config,
                             const std::filesystem::path& directory = {});

}  // namespace terraingen
