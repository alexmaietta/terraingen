/// @file
/// @brief Building library objects from configuration specs.
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

/// The `Surface` a spec describes: a plane, or a sphere of the given radius.
/// @throws ConfigError at "surface.radius" for a sphere without a positive, finite radius.
Surface BuildSurface(const SurfaceSpec& spec);

/// The density, ages, profile and degradation models a crater layer selects.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for an invalid model.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

/// The terrain a configuration describes, ready to `Evaluate` over the source's grid or any grid
/// on its lattice. Checks the configuration with `ValidateConfig` first.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError if the configuration is invalid.
Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory = {});

/// The albedo `config.albedo` describes for `terrain`, whose top-level crater layers crater
/// albedo layers read. Checks the albedo with `ValidateAlbedo` first.
/// @param terrain The terrain `BuildTerrain` built from `config`.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError if the albedo is invalid or the configuration has none.
/// @throws GeoTiffError if an albedo map cannot be read.
Albedo BuildAlbedo(const TerrainConfig& config, const Terrain& terrain,
                   const std::filesystem::path& directory = {});

/// Steps shared by validation, building and output writing.
namespace config {

/// `path` resolved against `directory`; an empty `directory` leaves it relative.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// Whether `path` has a mesh extension the outputs write: .glb or .ply.
bool IsMeshFile(const std::filesystem::path& path);

/// The boulder options a layer describes, unchecked, with its size distributions built.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.sizes or .ejecta_sizes for an invalid distribution.
BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path);

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

}  // namespace config

}  // namespace terraingen
