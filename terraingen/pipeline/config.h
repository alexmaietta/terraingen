#pragma once

#include "terraingen/bodies/moon/specifications.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/filters/rim_smoothing.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/region.h"
#include "terraingen/layers/rocks/rock_layer.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/synthetic_terrain.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

/// Version of the configuration format that this library reads and writes.
inline constexpr int kConfigSchemaVersion = 1;

/// One noise layer; when `hurst` is set, the octave gain is derived from it (`GainFromHurst`)
/// instead of `layer.fractal.gain`.
struct NoiseLayerConfig {
    NoiseLayer layer{};
    std::optional<float> hurst;

    /// `layer` with the gain from `hurst` applied.
    NoiseLayer Effective() const;

    bool operator==(const NoiseLayerConfig&) const = default;
};

/// "flat": a level datum at `elevation` over a `width` x `height` grid of `pixelSize` metres,
/// centred on the origin of the surface coordinates (`FlatSource`).
struct FlatSourceSpec {
    std::size_t width = 513;
    std::size_t height = 513;
    double pixelSize = 2.0;
    float elevation = 0.0f;

    bool operator==(const FlatSourceSpec&) const = default;
};

/// "raster": a heightmap read from a GeoTIFF, in its own projected coordinates (`GeoTiffSource`).
///
/// Relative paths resolve against the configuration's folder. The raster can be resampled to
/// `pixelSize` metres over the same extent, as when upscaling a DEM; layers then add relief in its
/// coordinates, which must be a projection in metres.
struct RasterSourceSpec {
    std::string path;
    std::size_t band = 0;             ///< From 0.
    std::optional<double> pixelSize;  ///< Resample to this cell size; unset keeps the file's.
    ResampleMethod resampling = ResampleMethod::Cubic;
    std::optional<double> scale{};   ///< Replaces the band's scale from the file's GDAL metadata.
    std::optional<double> offset{};  ///< Replaces the band's offset from the file's GDAL metadata.

    bool operator==(const RasterSourceSpec&) const = default;
};

struct ChainSourceSpec;

using SourceSpec = std::variant<FlatSourceSpec, RasterSourceSpec, ChainSourceSpec>;

/// "chain": `sources` tried in order, each filling where those before it have no data
/// (`SourceChain`); the terrain covers the first source's grid.
struct ChainSourceSpec {
    std::vector<SourceSpec> sources;

    bool operator==(const ChainSourceSpec&) const = default;
};

/// "noise": a stack of noise layers added to the terrain (`NoiseRelief`), from 1 to
/// `kMaxNoiseLayers`, measured from the source's reference point.
struct NoiseLayerSpec {
    std::vector<NoiseLayerConfig> stack{NoiseLayerConfig{}};

    /// The stack as `SyntheticTerrainParams`, with each layer's Hurst exponent applied.
    SyntheticTerrainParams Params() const;

    bool operator==(const NoiseLayerSpec&) const = default;
};

/// Crater densities, named in JSON by their "type".
struct LunarSurfaceSpec {  ///< "lunar_surface": `LunarSurface(ageGyr)`, density and ages.
    double ageGyr = 3.5;
    bool operator==(const LunarSurfaceSpec&) const = default;
};
struct PowerLawSpec {  ///< "power_law": `PowerLawSfd`, N per m^2 with D in metres.
    double coefficient = 0.1;
    double exponent = 2.0;
    bool operator==(const PowerLawSpec&) const = default;
};
struct PiecewisePowerLawSpec {  ///< "piecewise_power_law": `PiecewisePowerLawSfd`.
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
    bool operator==(const PiecewisePowerLawSpec&) const = default;
};
struct TabulatedSpec {  ///< "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Metres per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square metres per table area unit; 1e6 for km^2.
    bool operator==(const TabulatedSpec&) const = default;
};
struct TraskSpec {  ///< "trask": `TraskEquilibrium`.
    bool operator==(const TraskSpec&) const = default;
};
struct HartmannSpec {  ///< "hartmann": `HartmannEquilibrium`.
    bool operator==(const HartmannSpec&) const = default;
};
struct MooreSpec {  ///< "moore": `MooreDistribution(terrain)`.
    MooreTerrain terrain = MooreTerrain::RoughMare;
    bool operator==(const MooreSpec&) const = default;
};
struct ViperSpec {  ///< "viper": `ViperSpecification`.
    bool operator==(const ViperSpec&) const = default;
};
using DensitySpec = std::variant<LunarSurfaceSpec, PowerLawSpec, PiecewisePowerLawSpec,
                                 TabulatedSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec>;

/// Crater age models.
struct AutoAgesSpec {  ///< "auto": the lunar surface's steady-state ages, otherwise uniform 1 Gyr.
    bool operator==(const AutoAgesSpec&) const = default;
};
struct FreshAgesSpec {  ///< "fresh": `FreshAges`.
    bool operator==(const FreshAgesSpec&) const = default;
};
struct UniformAgesSpec {  ///< "uniform": `UniformAges(lifetimeGyr)`.
    double lifetimeGyr = 1.0;
    bool operator==(const UniformAgesSpec&) const = default;
};
/// Equilibrium densities for `SteadyStateAgesSpec`.
enum class EquilibriumModel { Trask, Hartmann };
struct SteadyStateAgesSpec {  ///< "steady_state": `SteadyStateAges` with the Neukum production.
    double ageGyr = 3.5;
    EquilibriumModel equilibrium = EquilibriumModel::Trask;
    bool extendedProduction = true;  ///< Extend the production function below 10 m.
    bool operator==(const SteadyStateAgesSpec&) const = default;
};
using AgesSpec = std::variant<AutoAgesSpec, FreshAgesSpec, UniformAgesSpec, SteadyStateAgesSpec>;

/// Degradation models.
struct NoDegradationSpec {  ///< "none": `NoDegradation`, optionally with a fixed d/D.
    std::optional<double> depthRatio;
    bool operator==(const NoDegradationSpec&) const = default;
};
struct FreshnessDegradationSpec {  ///< "freshness": `FreshnessDegradation`.
    double cutoffDepthRatio = 0.04;
    bool operator==(const FreshnessDegradationSpec&) const = default;
};
using DegradationSpec = std::variant<FreshnessDegradationSpec, NoDegradationSpec>;

/// Crater profiles.
struct FassettThomsonSpec {  ///< "fassett_thomson": `FassettThomsonProfile(rim)`.
    FassettThomsonRim rim = FassettThomsonRim::Original;
    bool operator==(const FassettThomsonSpec&) const = default;
};
struct MartinParkesDunstanSpec {  ///< "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
    bool operator==(const MartinParkesDunstanSpec&) const = default;
};
using ProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;

/// "craters": craters generated over the grid and applied to it (`CraterLayer`).
struct CraterLayerSpec {
    bool generate = true;  ///< Generate craters from `density`; otherwise only `manual` ones.
    std::uint32_t seed = 1;
    DensitySpec density{};
    AgesSpec ages{};
    double minDiameter = 4.0;    ///< Metres.
    double maxDiameter = 250.0;  ///< Metres.
    CraterSpacing spacing{};
    std::size_t maxCraters = 0;  ///< Keep at most this many generated craters; 0 keeps all.
    /// Manual craters, in the surface's coordinates; parsing sets their `manual` flag.
    std::vector<Crater> manual;
    ProfileSpec profile{};
    DegradationSpec degradation{};
    CraterApplyOptions apply{};

    bool operator==(const CraterLayerSpec&) const = default;
};

/// "rocks": rocks scattered over the terrain (`RockLayer`). Ejecta come from the nearest crater
/// layer before this one, which must exist when `rocks.ejecta.minCraterDiameter` is positive.
/// `rocks.cacheRocks` is not part of the configuration.
struct RockLayerSpec {
    RockLayerOptions rocks{};

    bool operator==(const RockLayerSpec&) const = default;
};

/// "roughness": fine roughness of a given RMS slope (`RoughnessLayer`).
struct RoughnessLayerSpec {
    RoughnessOptions roughness{};

    bool operator==(const RoughnessLayerSpec&) const = default;
};

/// "scale": height * `scale` + `offset` (`ScaleLayer`).
struct ScaleLayerSpec {
    double scale = 1.0;
    double offset = 0.0;

    bool operator==(const ScaleLayerSpec&) const = default;
};

/// "clamp": heights clamped to [`min`, `max`], either optional (`ClampLayer`).
struct ClampLayerSpec {
    std::optional<double> min;
    std::optional<double> max;

    bool operator==(const ClampLayerSpec&) const = default;
};

/// Region shapes, named in JSON by their "type".
struct RectangleRegionSpec {  ///< "rectangle": [`minX`, `maxX`] x [`minY`, `maxY`].
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool operator==(const RectangleRegionSpec&) const = default;
};
struct CircleRegionSpec {  ///< "circle": centred on (`x`, `y`).
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;
    bool operator==(const CircleRegionSpec&) const = default;
};
using RegionShapeSpec = std::variant<RectangleRegionSpec, CircleRegionSpec>;

/// A value held by pointer, so that a spec can hold the variant it belongs to; it is copied and
/// compared by value.
template <typename T> class Boxed {
  public:
    Boxed() : value_(std::make_unique<T>()) {}
    /// From anything `T` can be made from, such as one alternative of a variant.
    template <typename U>
        requires(!std::same_as<std::remove_cvref_t<U>, Boxed> && std::constructible_from<T, U &&>)
    Boxed(U&& value)  // NOLINT(google-explicit-constructor): converts implicitly by design
        : value_(std::make_unique<T>(std::forward<U>(value))) {}
    Boxed(const Boxed& other) : value_(std::make_unique<T>(*other.value_)) {}
    Boxed(Boxed&& other) noexcept = default;
    Boxed& operator=(const Boxed& other) {
        value_ = std::make_unique<T>(*other.value_);
        return *this;
    }
    Boxed& operator=(Boxed&& other) noexcept = default;
    ~Boxed() = default;

    T& operator*() {
        return *value_;
    }
    const T& operator*() const {
        return *value_;
    }
    T* operator->() {
        return value_.get();
    }
    const T* operator->() const {
        return value_.get();
    }

    friend bool operator==(const Boxed& a, const Boxed& b) {
        return *a.value_ == *b.value_;
    }

  private:
    std::unique_ptr<T> value_;
};

struct RegionLayerSpec;

using LayerSpec = std::variant<NoiseLayerSpec, CraterLayerSpec, RockLayerSpec, RoughnessLayerSpec,
                               ScaleLayerSpec, ClampLayerSpec, RegionLayerSpec>;

/// "region": `layer` applied only inside `shape`, feathered over `feather` metres outside it
/// (`RegionLayer`). Rock ejecta do not see crater layers inside regions.
struct RegionLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};

    bool operator==(const RegionLayerSpec&) const = default;
};

/// Filters, named in JSON by their "type": "rim_smoothing" is `RimSmoothingFilter`.
using FilterSpec = std::variant<RimSmoothing>;

/// Formats for crater lists.
enum class CraterListFormat { Csv, Json };

/// "crater_list": writes the generated and manual craters.
struct CraterListOutput {
    std::string path;
    CraterListFormat format = CraterListFormat::Csv;
    bool operator==(const CraterListOutput&) const = default;
};
/// "heightmap": writes the generated heights as a float32 GeoTIFF, with a raster source's
/// projection.
struct HeightmapOutput {
    std::string path;
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;
    bool operator==(const HeightmapOutput&) const = default;
};

using OutputSpec = std::variant<HeightmapOutput, CraterListOutput>;

/// Everything needed to generate a terrain: a source, the layers added to it in order, the filters
/// applied after them in order, and the outputs to write.
struct TerrainConfig {
    int schemaVersion = kConfigSchemaVersion;
    SourceSpec source{};
    std::vector<LayerSpec> layers;
    std::vector<FilterSpec> filters;
    std::vector<OutputSpec> outputs;

    bool operator==(const TerrainConfig&) const = default;
};

}  // namespace terraingen
