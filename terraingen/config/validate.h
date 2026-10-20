/// @file
/// @brief Checking a configuration before building it: everything parsing cannot catch.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace terraingen {

/// What a valid configuration generates, as `ValidateConfig` finds it.
struct ConfigSummary {
    std::optional<RasterGrid> base;  ///< The base's grid; none without a base.
    /// The grid each output covers, in order, an output of every level of a region once per level
    /// (`config::ResolveOutputs`); none for a mesh, a nested mesh or a manifest.
    std::vector<std::optional<RasterGrid>> outputGrids;
    /// Craters the top-level crater layers are expected to generate over the output grid with the
    /// most; unset when none generate or no output covers a grid.
    std::optional<double> expectedCraters;
};

/// Checks every number of a configuration against the range its field declares (`FieldRange`),
/// in the units the configuration stores it in.
/// @throws ConfigError naming the first number out of its range by its JSON path.
void CheckConfigRanges(const TerrainConfig& config);

/// Checks every field range (`CheckConfigRanges`) and everything that parsing cannot, such as grid
/// limits, the raster file, model parameters, outputs and their grids, and the expected crater
/// counts over each, and returns the grids and the largest count.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainConfig& config,
                             const std::filesystem::path& directory = {});

/// The grid a base covers, reading only the header of a raster's file; a chain's is its first
/// source's. On a body grids are in degrees, a pixel size in metres along a meridian.
/// @param directory Folder relative paths resolve against.
/// @throws ConfigError under "base" for an unreadable raster or invalid grid parameters.
RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory = {},
                          const Surface& surface = {});

/// Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainConfig& config, const std::filesystem::path& directory);

}  // namespace terraingen
