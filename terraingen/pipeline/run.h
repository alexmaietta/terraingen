#pragma once

#include "terraingen/albedo/albedo.h"
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

/// The albedo `config.albedo` describes for `terrain` (built by `BuildTerrain` from `config`):
/// crater albedo layers read the terrain's top-level crater layers, and synthetic noise is
/// measured from the source's reference point. Relative input paths resolve against `directory`.
/// @throws ConfigError if the configuration is invalid or has no albedo.
/// @throws GeoTiffError if an albedo map cannot be read.
Albedo BuildAlbedo(const TerrainConfig& config, const Terrain& terrain,
                   const std::filesystem::path& directory = {});

/// A generated terrain: heights, the features the layers placed, the projection keys of a raster
/// source, and the albedo bands when the configuration has an albedo.
struct GeneratedTerrain {
    Raster<float> heights;
    TerrainFeatures features;
    GeoKeys geoKeys;
    std::vector<Raster<float>> albedo{};
};

/// Generates the terrain `config` describes over its source's grid: `BuildTerrain`, then
/// `Evaluate` (and `EvaluateAlbedo` with an albedo, at the grid's cell size) on up to `threads`
/// threads (0 for every hardware thread; the result does not depend
/// on it). Relative input paths resolve against `directory`.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if a raster cannot be read.
GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory = {}, unsigned threads = 0);

/// Writes `craters` as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON ({"craters":
/// [{"x": ..., "y": ..., ...}]}), with doubles written exactly. With an `attributes` entry for
/// every crater, each also gets floor_depth, rim_height, depth_ratio, freshness and complex.
/// @throws std::runtime_error if the file cannot be written, or for `attributes` that are neither
/// empty nor one per crater.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     CraterListFormat format, const std::vector<CraterAttributes>& attributes = {});

/// Writes `rocks` as CSV (columns x, y, ground, diameter, height, burial, yaw, lean, id, model) or
/// as JSON ({"rocks": [...]}), doubles written exactly and an unknown ground left empty (CSV) or
/// null (JSON).
/// @throws std::runtime_error if the file cannot be written.
void WriteRockList(const std::vector<Rock>& rocks, const std::filesystem::path& path,
                   CraterListFormat format);

/// Writes `boulders` as a glTF scene (a path ending in .glb), each shape once with crease normals
/// split at `creaseAngle` degrees and drawn once per boulder, or (.ply) as one mesh of every
/// boulder placed in the world.
/// @throws std::invalid_argument for another extension, no boulders or an angle outside [0, 180].
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle = 35.0);

/// Writes `config`'s outputs (heightmaps and albedos with `terrain.geoKeys`, crater lists),
/// resolving relative paths against `directory`, and returns the paths written.
/// @throws std::runtime_error if an output cannot be written, or an albedo output has no albedo.
std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
