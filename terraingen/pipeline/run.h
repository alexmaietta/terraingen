/// @file
/// @brief Validating, building, generating and writing the terrain a configuration describes.
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
#include <optional>
#include <string>
#include <vector>

namespace terraingen {

/// Largest grid side a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// @brief Builds the models a crater layer selects.
/// @param craters The crater layer.
/// @return Its density, ages, profile and degradation.
/// @throws std::invalid_argument or std::domain_error for invalid model parameters.
CraterModels BuildCraterModels(const CraterLayerSpec& craters);

/// @brief The grid a source covers, reading only the header of a raster source's file.
///
/// Pixel sizes are metres; on a sphere grids are in longitude and latitude, so a size becomes
/// degrees along a meridian.
/// @param source The source.
/// @param directory Folder relative paths resolve against.
/// @param surface The surface the grid lies on.
/// @return The source's grid; a chain's is its first source's.
/// @throws ConfigError for an unreadable raster or invalid grid parameters.
RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory = {},
                          const Surface& surface = {});

/// @brief The `Surface` a spec describes.
/// @param spec The surface spec.
/// @return A plane, or a sphere of the given or the body's radius.
/// @throws ConfigError for a sphere without a positive, finite radius, or with both a radius and
/// a body.
Surface BuildSurface(const SurfaceSpec& spec);

/// What a valid configuration generates, as `ValidateConfig` finds it.
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

/// @brief The terrain a configuration describes, as library objects: its source, layers and
/// filters, ready to `Evaluate` over the source's grid or any grid on its lattice. Noise layers
/// measure positions from the source's reference point.
/// @param config The configuration, checked with `ValidateConfig` first.
/// @param directory Folder relative input paths resolve against.
/// @return The terrain.
/// @throws ConfigError if the configuration is invalid.
Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory = {});

/// @brief The albedo `config.albedo` describes for a terrain: crater albedo layers read the
/// terrain's top-level crater layers, and synthetic noise is measured from the source's
/// reference point.
/// @param config The configuration.
/// @param terrain The terrain `BuildTerrain` built from `config`.
/// @param directory Folder relative input paths resolve against.
/// @return The albedo.
/// @throws ConfigError if the albedo is invalid or the configuration has none.
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
    double seconds = 0.0;  ///< Wall-clock time the heights and albedo took to evaluate.
};

/// @brief Generates the terrain a configuration describes over its source's grid: `BuildTerrain`,
/// then `Evaluate` (and `EvaluateAlbedo` with an albedo, at the grid's cell size).
/// @param config The configuration.
/// @param directory Folder relative input paths resolve against.
/// @param threads Threads to use, 0 for every hardware thread; the result does not depend on it.
/// @return The generated terrain.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if a raster cannot be read.
GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory = {}, unsigned threads = 0);

/// @brief One line describing a generated terrain.
/// @param terrain The generated terrain.
/// @return "Generated a WxH terrain with N craters and M boulders in S s".
std::string Summary(const GeneratedTerrain& terrain);

/// @brief Writes craters as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON
/// ({"craters": [{"x": ..., "y": ..., ...}]}), with doubles written exactly. With an attributes
/// entry for every crater, each also gets floor_depth, rim_height, depth_ratio, freshness and
/// complex.
/// @param craters The craters.
/// @param path The file to write.
/// @param format CSV or JSON.
/// @param attributes Empty, or one entry per crater.
/// @throws std::invalid_argument for `attributes` that are neither empty nor one per crater.
/// @throws std::runtime_error if the file cannot be written.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes = {});

/// @brief Writes boulders as CSV (columns x, y, ground, diameter, height, burial, yaw, lean, id,
/// model) or as JSON ({"boulders": [...]}), doubles written exactly and an unknown ground left
/// empty (CSV) or null (JSON).
/// @param boulders The boulders.
/// @param path The file to write.
/// @param format CSV or JSON.
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderList(const std::vector<Boulder>& boulders, const std::filesystem::path& path,
                      ListFormat format);

/// @brief Writes boulder models as a glTF scene (a path ending in .glb), each shape once with
/// crease normals and drawn once per boulder, or (.ply) as one mesh of every boulder placed in
/// the world. On a sphere the world is the east-north-up frame of `site` on the datum sphere
/// (`PlacementAtSite`), so coordinates stay small; `WriteOutputs` uses the grid's centre.
/// @param boulders The boulder models.
/// @param path The file to write, ending in .glb or .ply.
/// @param creaseAngle Angle in degrees, from 0 to 180, above which .glb normals are split.
/// @param surface The surface the boulders lie on.
/// @param site The site whose frame a sphere's boulders are placed in.
/// @throws std::invalid_argument for another extension, no boulders or an angle outside [0, 180].
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle = 35.0,
                        const Surface& surface = {}, const Vec2d& site = {});

/// @brief Writes a configuration's outputs: heightmaps and albedos with `terrain.geoKeys`, crater
/// and boulder lists, boulder models and body meshes.
/// @param config The configuration.
/// @param terrain The terrain `GenerateTerrain` generated from `config`.
/// @param directory Folder relative output paths resolve against.
/// @return The paths written.
/// @throws std::runtime_error if an output cannot be written, or an albedo output has no albedo.
std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
