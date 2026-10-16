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

/// The `Surface` a spec describes: a plane, a sphere, an ellipsoid or a shape model, whose file is
/// found relative to `directory` and loaded once per file, modification time, Q and unit.
/// @throws ConfigError at "surface.radius" or "surface.radii" for radii that are not positive and
/// finite, and at "surface.path", "surface.q" or "surface.metres_per_unit" for a shape model that
/// cannot be loaded or a Q or unit out of range.
Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory = {});

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

/// The boulder options a layer describes, unchecked, with its size distributions built and its
/// shape files read.
/// @param path The layer's JSON path, which errors are reported under.
/// @param directory Folder the shape files resolve against.
/// @throws ConfigError at `path`.sizes, .ejecta_sizes or .shape_files[i] for an invalid
/// distribution or an unreadable mesh.
BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path,
                                        const std::filesystem::path& directory = {});

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

}  // namespace config

}  // namespace terraingen
