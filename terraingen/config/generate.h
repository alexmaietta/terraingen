/// @file
/// @brief Generating the terrain a configuration describes and writing its outputs.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/layers/features.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <filesystem>
#include <string>
#include <vector>

namespace terraingen {

/// @brief A terrain generated from a configuration.
struct GeneratedTerrain {
    Raster<float> heights;     ///< The heights over the source's grid.
    TerrainFeatures features;  ///< The features the layers placed.
    /// The projection of a raster source, or geographic keys on a sphere without one.
    GeoKeys geoKeys;
    /// The albedo bands; empty without an albedo.
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
