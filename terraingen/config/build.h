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
#include <optional>
#include <string>

namespace terraingen {

/// The `Surface` a spec describes: a plane, a sphere, an ellipsoid or a shape model, whose file is
/// found relative to `directory` and loaded once per file, modification time, Q and unit.
/// @throws ConfigError at "body.radius" or "body.radii" for radii that are not positive and
/// finite, and at "body.path", "body.q" or "body.metres_per_unit" for a shape model that
/// cannot be loaded or a Q or unit out of range.
Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory = {});

/// The density, ages, profile and degradation models a crater layer selects.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for an invalid model.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

/// The terrain a configuration describes, ready to `Evaluate` over any grid or at points. Checks
/// the configuration with `ValidateConfig` first.
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

/// The field a spec describes: a constant, or its GeoTIFF read.
/// @param path The spec's JSON path, which errors are reported under.
/// @param directory Folder the GeoTIFF resolves against.
/// @throws ConfigError at `path` for an unreadable GeoTIFF or invalid read options.
std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory = {});

/// The density map a spec describes, its field built (`BuildField`) and the map checked.
/// @throws ConfigError at `path` for an invalid map or field.
DensityMap BuildDensityMap(const DensityMapSpec& spec, const std::string& path,
                           const std::filesystem::path& directory = {});

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

/// The Hapke parameters of each material class a spec describes, unchecked: its reference's,
/// replaced by any it gives.
MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec);

/// An output that lists features over a region at a sample spacing: a list or boulder models.
template <typename T>
concept FeatureOutput = requires(const T& output) {
    output.region;
    output.spacing;
};

/// The grid a region describes on `surface`, unchecked; on a body its cells are `pixelSize` metres
/// along a meridian.
RasterGrid RegionGrid(const GridRegionSpec& region, const Surface& surface);

/// The whole of a body in longitude and latitude, in square cells of at most `spacing` metres
/// along a meridian, for listing features over it.
/// @param spacing In metres; positive.
RasterGrid WholeBodyGrid(const Surface& surface, double spacing);

/// The grid an output covers: its region, or else `base`, the base's grid; for a list or boulder
/// models without either on a body, the whole body at its spacing. None for a mesh, or for an
/// output without a grid to cover, which `ValidateConfig` rejects.
std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& base, const Surface& surface);

}  // namespace config

}  // namespace terraingen
