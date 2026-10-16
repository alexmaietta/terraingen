/// @file
/// @brief Generating the terrain a configuration describes and writing its outputs.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/pipeline/features.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <filesystem>
#include <string>
#include <vector>

namespace terraingen {

/// A terrain's heights, and its albedo bands, evaluated over one grid.
struct GeneratedGrid {
    Raster<float> heights{};  ///< The heights over the grid.
    /// The albedo bands; empty unless an albedo output covers the grid.
    std::vector<Raster<float>> albedo{};
};

/// A terrain generated from a configuration.
struct GeneratedTerrain {
    Terrain terrain;  ///< The terrain built from the configuration, to list its features from.
    /// One per distinct grid a heightmap, albedo or terrain mesh output covers, in output order.
    std::vector<GeneratedGrid> grids{};
    /// The projection of a raster base, or geographic keys on a body without one.
    GeoKeys geoKeys{};
    double seconds = 0.0;  ///< Wall-clock time the heights and albedo took to evaluate.

    /// The generated grid whose heights cover `grid`, or null.
    const GeneratedGrid* Find(const RasterGrid& grid) const;
};

/// Generates the terrain a configuration describes over each grid its heightmap, albedo and
/// terrain mesh outputs cover: `BuildTerrain`, then `Evaluate`, and `BuildAlbedo` and
/// `EvaluateAlbedo` for albedo outputs.
/// @param directory Folder relative input paths resolve against.
/// @param threads Threads to use, 0 for every hardware thread; the result does not depend on it.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if a raster cannot be read.
GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory = {}, unsigned threads = 0);

/// One line describing a generated terrain, such as "Generated a WxH terrain in S s".
std::string Summary(const GeneratedTerrain& terrain);

/// Writes craters as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON
/// ({"craters": [{"x": ..., "y": ..., ...}]}), with doubles written exactly. With an attributes
/// entry for every crater, each also gets floor_depth, rim_height, depth_ratio, freshness and
/// complex.
/// @throws std::invalid_argument for `attributes` that are neither empty nor one per crater.
/// @throws std::runtime_error if the file cannot be written.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes = {});

/// Writes boulders as CSV (columns x, y, ground, diameter, height, burial, yaw, lean, id,
/// model) or as JSON ({"boulders": [...]}), doubles written exactly and an unknown ground left
/// empty (CSV) or null (JSON).
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderList(const std::vector<Boulder>& boulders, const std::filesystem::path& path,
                      ListFormat format);

/// Writes boulder models as a scene (`WriteScene`): each shape once with crease normals and its
/// material, drawn once per boulder (glTF), one object per boulder (OBJ) or one merged mesh
/// (STL). On a sphere the world is the east-north-up frame of `site` on the datum sphere
/// (`PlacementAtSite`), so coordinates stay small; `WriteOutputs` uses the grid's centre.
/// @param creaseAngle Angle in degrees, from 0 to 180, above which normals are split.
/// @param instanced Whether shared shapes are written once and drawn per boulder; otherwise each
/// boulder is written as its own mesh, already placed, for tools without instancing.
/// @throws std::invalid_argument for no boulders, an angle outside [0, 180] or as `WriteScene`.
/// @throws std::runtime_error if the file cannot be written.
void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle = 60.0,
                        const Surface& surface = {}, const Vec2d& site = {}, bool instanced = true);

/// Writes a configuration's outputs and returns the paths written: heightmaps and albedos
/// with `terrain.geoKeys`, terrain meshes, crater and boulder lists, boulder models and meshes,
/// each over the grid it covers (`config::OutputGrid`).
/// @param terrain The terrain `GenerateTerrain` generated from `config`.
/// @param directory Folder relative output paths resolve against.
/// @throws std::runtime_error if an output cannot be written, or `terrain` has not generated the
/// grid or albedo it needs.
std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
