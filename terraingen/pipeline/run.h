#pragma once

#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <filesystem>
#include <vector>

namespace terraingen {

/// Largest grid side a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// Builds the models `craters` selects.
/// @throws std::invalid_argument or std::domain_error for invalid model parameters.
CraterModels BuildCraterModels(const CraterLayerSpec& craters);

/// The grid `source` covers, reading only the header of a raster source's file; relative paths
/// resolve against `directory`.
/// @throws ConfigError for an unreadable raster or invalid grid parameters.
RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory = {});

/// Checks everything that parsing cannot: grid limits, the raster file, noise parameters, model
/// parameters, crater and filter options, outputs and the expected crater count against
/// `CraterGenerationOptions` limits. Relative input paths resolve against `directory`.
/// @throws ConfigError naming the first invalid value by its JSON path.
void ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory = {});

/// The terrain `config` describes: its source, layers and filters as library objects, ready to
/// `Evaluate` over the source's grid or any grid on its lattice. Noise layers measure positions
/// from the source's reference point. Relative input paths resolve against `directory`.
/// @throws ConfigError if the configuration is invalid.
Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory = {});

/// A generated terrain: heights, the features the layers placed and the projection keys of a
/// raster source.
struct GeneratedTerrain {
    Raster<float> heights;
    TerrainFeatures features;
    GeoKeys geoKeys;
};

/// Generates the terrain `config` describes over its source's grid: `BuildTerrain`, then
/// `Evaluate`. Relative input paths resolve against `directory`.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if a raster cannot be read.
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
