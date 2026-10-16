/// @file
/// @brief Checking a configuration before building it: everything parsing cannot catch.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <filesystem>
#include <optional>

namespace terraingen {

/// Largest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// What a valid configuration generates, as `ValidateConfig` finds it.
struct ConfigSummary {
    RasterGrid grid;  ///< The source's grid.
    /// Craters the top-level crater layers are expected to generate; unset when none generate.
    std::optional<double> expectedCraters;
};

/// Checks everything that parsing cannot, such as grid limits, the raster file, model parameters,
/// outputs and the expected crater count, and returns the grid and that count.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainConfig& config,
                             const std::filesystem::path& directory = {});

/// The grid a source covers, reading only the header of a raster source's file; a chain's is
/// its first source's. On a sphere grids are in degrees, a pixel size in metres along a meridian.
/// @param directory Folder relative paths resolve against.
/// @throws ConfigError at the source's path for an unreadable raster or invalid grid parameters.
RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory = {},
                          const Surface& surface = {});

/// Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainConfig& config, const std::filesystem::path& directory,
                    const Surface& surface);

}  // namespace terraingen
