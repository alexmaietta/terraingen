/// @file
/// @brief Building library objects from configuration specs.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/region.h"
#include "terraingen/pipeline/generator.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
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

/// The albedo `config.albedo` describes for `generator`, whose top-level crater layers crater
/// albedo layers read. Checks the albedo with `ValidateAlbedo` first.
/// @param generator The generator `BuildGenerator` built from `config`.
/// @param directory Folder relative input paths resolve against.
/// @throws ConfigError if the albedo is invalid or the configuration has none.
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

/// Whether `T` has members `region` and `spacing`.
template <typename T, typename = void> struct FeatureOutputTrait : std::false_type {};
template <typename T>
struct FeatureOutputTrait<T, std::void_t<decltype(std::declval<const T&>().region),
                                         decltype(std::declval<const T&>().spacing)>>
    : std::true_type {};

/// An output that lists features over a region at a sample spacing: a list or boulder models.
template <typename T> inline constexpr bool FeatureOutput = FeatureOutputTrait<T>::value;

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

/// The sample spacing in metres an output lists features at; 0 for its grid's, and for outputs
/// that list none.
double OutputSpacing(const OutputSpec& output);

/// Whether a variant, such as a layer or an output, holds one of the alternatives `Kinds`.
template <typename... Kinds, typename... Ts> bool HoldsAny(const std::variant<Ts...>& value) {
    return (std::holds_alternative<Kinds>(value) || ...);
}

/// The number of `layers` of the kinds `Kinds`; layers inside regions do not count.
template <typename... Kinds> std::size_t CountLayers(const std::vector<LayerSpec>& layers) {
    return static_cast<std::size_t>(
        std::count_if(layers.begin(), layers.end(),
                      [](const LayerSpec& layer) { return HoldsAny<Kinds...>(layer); }));
}

/// Whether any of `layers` is of the kinds `Kinds`; layers inside regions do not count.
template <typename... Kinds> bool HasLayer(const std::vector<LayerSpec>& layers) {
    return CountLayers<Kinds...>(layers) > 0;
}

/// The indices of the `layers` of the kinds `Kinds`, in order; layers inside regions do not count.
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
