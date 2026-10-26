// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Steps shared by configuration validation, building and output writing.
#pragma once

#include "config/spec.h"
#include "terraingen/config/build.h"
#include "terraingen/config/json.h"
#include "terraingen/config/validate.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/masked.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace terraingen {

/// The `Surface` a body spec describes, checking its field ranges first, as `BuildSurface` of a
/// configuration does.
/// @throws ConfigError at the field of "body" out of its range, at "body.radii_m" for not three
/// radii, and at "body.path" for a shape model that cannot be loaded.
Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory = {});

/// The density, ages, profile and degradation models a crater layer selects.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for a model whose
/// constructor rejects its parameters.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

/// A layer's JSON "type", with the inner layer's type for a masked one: "masked (boulders)".
std::string LayerType(const LayerSpec& layer);

/// Steps shared by validation, building and output writing. Validation and building are one pass
/// (`BuildCheckedGenerator`): every layer spec is checked, then each layer is built and checked
/// against what it built.
namespace config {

/// `path` resolved against `directory`; an empty `directory` leaves it relative.
std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory);

/// `seams` with their lengths, given in meters, in `surface`'s units.
inline ChainSeams SurfaceSeams(ChainSeams seams, const Surface& surface) {
    seams.width /= surface.UnitLength();
    seams.decayLength /= surface.UnitLength();
    return seams;
}

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

/// The inherited craters a spec describes, unchecked, their crater list read (`ReadCraterList`).
/// @throws ConfigError at `path`.path for an unreadable or malformed crater list.
InheritedCraters BuildInheritedCraters(const InheritedCratersSpec& spec, const std::string& path,
                                       const std::filesystem::path& directory = {});

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

/// The Hapke parameters of each material class a spec describes, unchecked: its reference's,
/// replaced by any it gives.
MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec);

/// The crater production function a configuration names.
std::shared_ptr<const SizeFrequencyDistribution> BuildProduction(ProductionFunction production);

/// Steady-state ages on the production surface and equilibrium `density` is the minimum of; none
/// for another density.
std::optional<SteadyStateAgesSpec> MatchingAges(const DensitySpec& density);

/// The ages a crater layer gives its craters: its `ages`, or when unset `MatchingAges` of its
/// density, else `SteadyStateAgesSpec`'s.
AgesSpec LayerAges(const CraterLayerSpec& craters);

/// `BuildAlbedo` for a configuration `ValidateConfig` has already checked.
Albedo BuildValidAlbedo(const ConfigSpec& config, const TerrainGenerator& generator,
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

/// Checks every number of a configuration against the range its field declares (`FieldRange`), in
/// the units the configuration stores it in, and that every number is finite.
/// @throws ConfigError naming the first number out of its range by its JSON path.
void CheckConfigRanges(const ConfigSpec& config);

/// Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const ConfigSpec& config, const std::filesystem::path& directory);

/// Builds a configuration's generator a part at a time, in the order one pass checks it.
class GeneratorBuilder {
  public:
    /// Starts a generator on `surface` with the configuration's source, or `external` in its
    /// place, which the layers then read as their measured data.
    /// @throws ConfigError under "source" for what the sources reject.
    GeneratorBuilder(const ConfigSpec& config, const std::filesystem::path& directory,
                     Surface surface, std::shared_ptr<const Source> external = nullptr);

    /// Builds layer `i`, the one after those built so far, and returns it.
    /// @throws ConfigError under the layer's path for what its parts reject.
    const Layer& AddLayer(std::size_t i);

    /// The generator built so far.
    TerrainGenerator Take() {
        return std::move(generator_);
    }

  private:
    const ConfigSpec& config_;
    const std::filesystem::path& directory_;
    TerrainGenerator generator_;
    std::shared_ptr<const Source> given_;
    std::shared_ptr<const Source> measured_;
    std::vector<std::shared_ptr<const CraterLayer>> craters_;
    std::shared_ptr<const BoulderLayer> boulders_;
};

/// A configuration's generator and summary, from one pass that checks each part as it builds it.
struct CheckedGenerator {
    TerrainGenerator generator;
    ConfigSummary summary;
};

/// Checks and builds `config` in one pass, on `external` in place of its source when given.
/// @throws ConfigError naming the first invalid value by its JSON path.
CheckedGenerator BuildCheckedGenerator(const ConfigSpec& config,
                                       const std::filesystem::path& directory,
                                       const std::shared_ptr<const Source>& external = nullptr);

}  // namespace config

}  // namespace terraingen
