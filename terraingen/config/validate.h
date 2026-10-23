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
    /// The grid each output covers (`config::OutputGrid`), in order; none for a body mesh or a
    /// report.
    std::vector<std::optional<RasterGrid>> outputGrids;
    /// Craters the top-level crater layers are expected to generate over the output grid with the
    /// most; unset when none generate or no output covers a grid.
    std::optional<double> expectedCraters;
};

/// Checks every field range (`config::CheckConfigRanges`) and everything that parsing cannot, such
/// as grid limits, the raster file, model parameters, outputs and their grids, and the expected
/// crater counts over each, and returns the grids and the largest count.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainGenConfig& config,
                             const std::filesystem::path& directory = {});

namespace config {

/// Checks every number of a configuration against the range its field declares (`FieldRange`), in
/// the units the configuration stores it in, and that every number is finite.
/// @throws ConfigError naming the first number out of its range by its JSON path.
void CheckConfigRanges(const TerrainGenConfig& config);

/// Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainGenConfig& config, const std::filesystem::path& directory);

}  // namespace config

}  // namespace terraingen
