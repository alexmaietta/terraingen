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

/// @brief Largest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// @brief What a valid configuration generates, as `ValidateConfig` finds it.
struct ConfigSummary {
    RasterGrid grid;  ///< The source's grid.
    /// Craters the top-level crater layers are expected to generate; unset when none generate.
    std::optional<double> expectedCraters;
};

/// @brief Checks everything that parsing cannot: grid limits, the raster file, noise parameters,
/// model parameters, crater and filter options, outputs and the expected crater count against
/// `CraterGenerationOptions` limits.
/// @param config The configuration.
/// @param directory Folder relative input paths resolve against.
/// @return The grid and the expected number of craters.
/// @throws ConfigError naming the first invalid value by its JSON path.
ConfigSummary ValidateConfig(const TerrainConfig& config,
                             const std::filesystem::path& directory = {});

/// @brief The grid a source covers, reading only the header of a raster source's file.
///
/// Pixel sizes are metres; on a sphere grids are in longitude and latitude, so a size becomes
/// degrees along a meridian.
/// @param source The source.
/// @param directory Folder relative paths resolve against.
/// @param surface The surface the grid lies on.
/// @return The source's grid; a chain's is its first source's.
/// @throws ConfigError at the source's path for an unreadable raster or invalid grid parameters.
RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory = {},
                          const Surface& surface = {});

/// @brief Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @param directory Folder relative input paths resolve against.
/// @param surface The configuration's surface.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainConfig& config, const std::filesystem::path& directory,
                    const Surface& surface);

}  // namespace terraingen
