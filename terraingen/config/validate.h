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

/// Checks every number of a configuration against the range its field declares (`FieldRange`),
/// in the units the configuration stores it in.
/// @throws ConfigError naming the first number out of its range by its JSON path.
void CheckConfigRanges(const TerrainGenConfig& config);

/// Checks every field range (`CheckConfigRanges`) and everything that parsing cannot, such as grid
/// limits, the raster file, model parameters, outputs and their grids, and the expected crater
/// counts over each, and returns the grids and the largest count.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainGenConfig& config,
                             const std::filesystem::path& directory = {});

/// The grid a source covers, reading only the header of a raster's file; a chain's is its first
/// source's, and synthetic noise has none. On a body grids are in degrees. Also checks the
/// sources' upsampling and synthetic stacks.
/// @param directory Folder relative paths resolve against.
/// @throws ConfigError under "source" for an unreadable raster, invalid grid parameters, an
/// unknown upsampling type or invalid settings.
std::optional<RasterGrid> DescribeSource(const SourceSpec& source,
                                         const std::filesystem::path& directory = {},
                                         const Surface& surface = {});

/// Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainGenConfig& config, const std::filesystem::path& directory);

}  // namespace terraingen
