#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/pipeline/config.h"
#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/cells.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace terraingen {

/// Largest grid side a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// The library models a `CraterConfig` selects.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
    std::shared_ptr<const DegradationModel> degradation;
    std::shared_ptr<const CraterProfile> profile;
};

/// Builds the models `craters` selects.
/// @throws std::invalid_argument or std::domain_error for invalid model parameters.
CraterModels BuildCraterModels(const CraterConfig& craters);

/// The grid a surface produces: its size in cells and its pixel-to-world transform.
struct SurfaceGrid {
    std::size_t width = 0;
    std::size_t height = 0;
    GeoTransform transform{};

    /// The axis-aligned box in surface coordinates that the grid covers.
    Bounds2d Bounds() const;

    /// Area of the grid in square world units.
    double Area() const;

    /// Side of a cell: the square root of a cell's area.
    double PixelSize() const;
};

/// The grid `surface` produces, reading only the header of a raster surface's file; relative paths
/// resolve against `directory`.
/// @throws ConfigError for an unreadable raster or invalid grid parameters.
SurfaceGrid DescribeSurface(const SurfaceSpec& surface,
                            const std::filesystem::path& directory = {});

/// Checks everything that parsing cannot: grid limits, the raster file, noise layer parameters,
/// model parameters, crater options, outputs and the expected crater count against
/// `CraterGenerationOptions` limits. Relative input paths resolve against `directory`.
/// @throws ConfigError naming the first invalid value by its JSON path.
void ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory = {});

/// A generated terrain: heights, the craters applied to them and the projection keys of a raster
/// surface.
struct GeneratedTerrain {
    Raster<float> heights;
    CraterList craters;
    GeoKeys geoKeys;
};

/// Generates the terrain `config` describes: the surface (noise layers, or a raster that is
/// resampled and given detail noise), then the craters generated, applied and rim-smoothed with the
/// configured models and options. Relative input paths resolve against `directory`.
///
/// Detail noise on a raster surface is evaluated in coordinates relative to the raster's origin, to
/// keep single-precision noise accurate far from the projection's origin.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if the raster cannot be read.
GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory = {});

/// Writes `craters` as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON ({"craters":
/// [{"x": ..., "y": ..., ...}]}), with doubles written exactly.
/// @throws std::runtime_error if the file cannot be written.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     CraterListFormat format);

/// Writes `config`'s outputs (heightmaps with `terrain.geoKeys`, crater lists), resolving relative
/// paths against `directory`, and returns the paths written.
/// @throws std::runtime_error if an output cannot be written.
std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
