// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Building a terrain's surface, terrain and albedo from a configuration.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/sources/source.h"

#include <filesystem>
#include <memory>

namespace terraingen {

/// The `Surface` a configuration's body describes: a plane, a sphere, an ellipsoid or a shape
/// model, loaded once per file, modification time, Q and unit.
/// @param directory Folder relative input paths resolve against; empty for the folder the
/// configuration was read from (`TerrainGenConfig::Directory`).
/// @throws ConfigError at the field of "body" that is invalid or cannot be loaded.
Surface BuildSurface(const TerrainGenConfig& config, const std::filesystem::path& directory = {});

/// The terrain a configuration describes, ready to `Terrain::Evaluate` over any grid or at
/// points, checked as `ValidateConfig` checks it in the same pass.
/// @param directory Folder relative input paths resolve against; empty for the folder the
/// configuration was read from (`TerrainGenConfig::Directory`).
/// @throws ConfigError if the configuration is invalid.
Terrain BuildTerrain(const TerrainGenConfig& config, const std::filesystem::path& directory = {});

/// The terrain a configuration describes, on `source` in place of a source of its own, such as
/// an embedder's DEM. The layers read `source` as measured data, as they read a raster source:
/// `source_resolves_px` and crater datums use it.
/// @throws ConfigError if the configuration is invalid or gives a source.
/// @throws std::invalid_argument for a null `source`, or one whose coordinates do not suit the
/// body (`Terrain::Terrain`).
Terrain BuildTerrain(const TerrainGenConfig& config, std::shared_ptr<const Source> source,
                     const std::filesystem::path& directory = {});

/// The albedo a configuration describes for `terrain`, whose top-level crater layers crater
/// albedo layers read. Checks the albedo first.
/// @param terrain The terrain `BuildTerrain` built from `config`.
/// @param directory Folder relative input paths resolve against; empty for the folder the
/// configuration was read from (`TerrainGenConfig::Directory`).
/// @throws ConfigError if the albedo is invalid, the configuration has none, or `terrain`'s
/// layers are not the ones `config` builds.
/// @throws GeoTiffError if an albedo map cannot be read.
Albedo BuildAlbedo(const TerrainGenConfig& config, const Terrain& terrain,
                   const std::filesystem::path& directory = {});

}  // namespace terraingen
