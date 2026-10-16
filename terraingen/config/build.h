/// @file
/// @brief Building library objects from configuration specs: the surface, crater models, terrain
/// and albedo.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/region.h"
#include "terraingen/pipeline/terrain.h"

#include <filesystem>
#include <string>

namespace terraingen {

/// @brief The `Surface` a spec describes.
/// @param spec The surface spec.
/// @return A plane, or a sphere of the given or the body's radius.
/// @throws ConfigError for a sphere without a positive, finite radius, or with both a radius and
/// a body.
Surface BuildSurface(const SurfaceSpec& spec);

/// @brief Builds the models a crater layer selects.
/// @param craters The crater layer.
/// @param path The layer's JSON path, which errors are reported under.
/// @return Its density, ages, profile and degradation.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for an invalid model.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

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

/// Steps shared by validation, building and output writing.
namespace config {

/// @brief A configuration path resolved against a folder.
/// @param path The path as the configuration gives it.
/// @param directory Folder a relative path resolves against; empty leaves it relative.
/// @return The resolved path.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// @brief Whether a path names a mesh file format the pipeline writes.
/// @param path The path.
/// @return True for a .glb or .ply extension.
bool IsMeshFile(const std::filesystem::path& path);

/// @brief The boulder options a layer describes, with its size distributions built.
/// @param spec The boulder layer.
/// @param path The layer's JSON path.
/// @return The options, unchecked.
/// @throws ConfigError at `path`.sizes or .ejecta_sizes for an invalid distribution.
BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path);

/// @brief The region shape a spec describes.
/// @param spec The shape spec.
/// @return The shape, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

}  // namespace config

}  // namespace terraingen
