// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Building library objects from configuration specs.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/masked.h"
#include "terraingen/pipeline/generator.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

/// The `Surface` a spec describes: a plane, a sphere, an ellipsoid or a shape model, whose file is
/// found relative to `directory` and loaded once per file, modification time, Q and unit. As it
/// is also used on its own, it checks the body's field ranges (`CheckRanges`) first.
/// @throws ConfigError at the field of "body" out of its range, at "body.radii" for not three
/// radii, and at "body.path" for a shape model that cannot be loaded.
Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory = {});

/// The density, ages, profile and degradation models a crater layer selects.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for a model whose
/// constructor rejects its parameters.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

/// The terrain generator a configuration describes, ready to `Evaluate` over any grid or at
/// points. Checks the configuration with `ValidateConfig` first.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError if the configuration is invalid.
TerrainGenerator BuildGenerator(const TerrainGenConfig& config,
                                const std::filesystem::path& directory = {});

/// The terrain generator a configuration describes, on `source` in place of a source of its own,
/// such as an embedder's DEM. The layers read `source` as measured data, as they read a raster
/// source: `source_resolves_px` and crater datums use it.
/// @throws ConfigError if the configuration is invalid or gives a source.
/// @throws std::invalid_argument for a null `source`.
TerrainGenerator BuildGenerator(const TerrainGenConfig& config,
                                std::shared_ptr<const Source> source,
                                const std::filesystem::path& directory = {});

/// The albedo `config.albedo` describes for `generator`, whose top-level crater layers crater
/// albedo layers read. Checks the albedo with `config::ValidateAlbedo` first.
/// @param generator The generator `BuildGenerator` built from `config`.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError if the albedo is invalid, the configuration has none, or `generator`'s
/// layers are not the ones `config` builds.
/// @throws GeoTiffError if an albedo map cannot be read.
Albedo BuildAlbedo(const TerrainGenConfig& config, const TerrainGenerator& generator,
                   const std::filesystem::path& directory = {});

/// Steps shared by validation, building and output writing.
///
/// Each `Build*` only builds: it turns a spec into library objects, reporting what their
/// constructors and file readers reject at the spec's JSON path, and leaves every other check to
/// `ValidateConfig`, which calls them to check what they build.
namespace config {

/// `path` resolved against `directory`; an empty `directory` leaves it relative.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// The boulder options a layer describes, unchecked, with its size distributions, maps and
/// completeness built and its shape files read.
/// @param path The layer's JSON path, which errors are reported under.
/// @param directory Folder the shape files resolve against.
/// @throws ConfigError at `path`.sizes, .ejecta_sizes or .shape_files[i] for an invalid
/// distribution or an unreadable mesh, and at a map's or the completeness' field for an unreadable
/// GeoTIFF.
BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path,
                                        const std::filesystem::path& directory = {});

/// The field a spec describes: a constant, or its GeoTIFF read.
/// @param path The spec's JSON path, which errors are reported under.
/// @param directory Folder the GeoTIFF resolves against.
/// @throws ConfigError at `path` for an unreadable GeoTIFF or invalid read options.
std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory = {});

/// The density map a spec describes, unchecked, its field built (`BuildField`).
/// @throws ConfigError at `path`.field for an unreadable GeoTIFF.
DensityMap BuildDensityMap(const DensityMapSpec& spec, const std::string& path,
                           const std::filesystem::path& directory = {});

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

/// The Hapke parameters of each material class a spec describes, unchecked: its reference's,
/// replaced by any it gives.
MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec);

/// The crater production function a configuration names.
std::shared_ptr<const SizeFrequencyDistribution> BuildProduction(ProductionFunction production);

/// `BuildAlbedo` for a configuration `ValidateConfig` has already checked.
Albedo BuildValidAlbedo(const TerrainGenConfig& config, const TerrainGenerator& generator,
                        const std::filesystem::path& directory);

/// The box an extent describes, unchecked: its `min` and `max` corners, or else `center` -
/// `size` / 2 to `center` + `size` / 2. None when neither pair has two numbers each.
std::optional<Bounds2d> ExtentBox(const ExtentSpec& extent);

/// The area an output covers, or null for one without an area, a body mesh or a report.
const OutputAreaSpec* OutputArea(const OutputSpec& output);

/// The extent an area gives on `surface`: `extentDeg` on a body, `extentM` on a plane.
const std::optional<ExtentSpec>& SurfaceExtent(const OutputAreaSpec& area, const Surface& surface);

/// Whether an output lists features: a crater or boulder list, or boulder models.
bool ListsFeatures(const OutputSpec& output);

/// The whole of a body in longitude and latitude, in square cells of at most `spacing` meters
/// along a meridian, for listing features over it.
/// @param spacing In meters; positive.
RasterGrid WholeBodyGrid(const Surface& surface, double spacing);

/// The box an output area covers and the side of its square cells, in the surface's units.
struct AreaCells {
    Bounds2d box{};
    double cell = 0.0;
};

/// What an area covers, unchecked: its extent, or else `source`'s bounds, in cells of its pixel
/// size, or else of `source`'s cell size. None when the area and `source` leave either unknown.
/// @param source The source's grid; none without a raster or chain source.
std::optional<AreaCells> CoveredCells(const OutputAreaSpec& area,
                                      const std::optional<RasterGrid>& source,
                                      const Surface& surface);

/// The grid an output covers: its `CoveredCells` snapped outward by `RasterGrid::Covering`, or
/// `source` itself when the area gives neither an extent nor a pixel size. A feature list without
/// an extent or `source` on a body covers the whole body at its pixel size (`WholeBodyGrid`). None
/// for an output without an area, or for one without a grid to cover, which `ValidateConfig`
/// rejects.
std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& source,
                                     const Surface& surface);

/// The sample spacing in meters an output lists features at: its pixel size for a list over the
/// whole body, otherwise 0 for its grid's own.
double OutputSpacing(const OutputSpec& output, const std::optional<RasterGrid>& source,
                     const Surface& surface);

/// Whether a variant, such as a layer or an output, holds one of the alternatives `Kinds`.
template <typename... Kinds, typename... Ts> bool HoldsAny(const std::variant<Ts...>& value) {
    return (std::holds_alternative<Kinds>(value) || ...);
}

/// The number of `layers` of the kinds `Kinds`; layers inside masked layers do not count.
template <typename... Kinds> std::size_t CountLayers(const std::vector<LayerSpec>& layers) {
    return static_cast<std::size_t>(
        std::count_if(layers.begin(), layers.end(),
                      [](const LayerSpec& layer) { return HoldsAny<Kinds...>(layer); }));
}

/// Whether any of `layers` is of the kinds `Kinds`; layers inside masked layers do not count.
template <typename... Kinds> bool HasLayer(const std::vector<LayerSpec>& layers) {
    return CountLayers<Kinds...>(layers) > 0;
}

/// The indices of the `layers` of the kinds `Kinds`, in order; layers inside masked layers do
/// not count.
template <typename... Kinds>
std::vector<std::size_t> LayerIndices(const std::vector<LayerSpec>& layers) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (HoldsAny<Kinds...>(layers[i])) {
            indices.push_back(i);
        }
    }
    return indices;
}

/// Whether any of `outputs` is of the kinds `Kinds`.
template <typename... Kinds> bool HasOutput(const std::vector<OutputSpec>& outputs) {
    return std::any_of(outputs.begin(), outputs.end(),
                       [](const OutputSpec& output) { return HoldsAny<Kinds...>(output); });
}

}  // namespace config

}  // namespace terraingen
