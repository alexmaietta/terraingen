/// @file
/// @brief The pipeline's steps from configuration specs to library objects.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/region.h"
#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/terrain.h"

#include <filesystem>
#include <string>

namespace terraingen {

/// @brief A configuration path resolved against a folder.
/// @param path The path as the configuration gives it.
/// @param directory Folder a relative path resolves against; empty leaves it relative.
/// @return The resolved path.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// @brief Whether a path names a mesh file format the pipeline writes.
/// @param path The path.
/// @return True for a .glb or .ply extension.
bool IsMeshFile(const std::filesystem::path& path);

/// @brief Builds the models a crater layer selects, each checked at its JSON key.
/// @param craters The crater layer.
/// @param path The layer's JSON path.
/// @return Its density, ages, profile and degradation.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for an invalid model.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path);

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

/// @brief The terrain a configuration that `ValidateConfig` accepted describes.
/// @param config The valid configuration.
/// @param directory Folder relative input paths resolve against.
/// @return The terrain.
/// @throws GeoTiffError if a raster source cannot be read.
Terrain BuildValidTerrain(const TerrainConfig& config, const std::filesystem::path& directory);

/// @brief The albedo a valid albedo spec describes for a terrain.
/// @param spec The valid albedo spec.
/// @param terrain The terrain built from the same configuration.
/// @param directory Folder relative input paths resolve against.
/// @return The albedo.
/// @throws GeoTiffError if an albedo map cannot be read.
Albedo BuildValidAlbedo(const AlbedoSpec& spec, const Terrain& terrain,
                        const std::filesystem::path& directory);

}  // namespace terraingen
