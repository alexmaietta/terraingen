#pragma once

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/bodies/moon/boulders.h"
#include "terraingen/bodies/moon/specifications.h"
#include "terraingen/bodies/radii.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/filters/rim_smoothing.h"
#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/region.h"
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
/// centred on (`centreX`, `centreY`) in surface coordinates, the origin by default (`FlatSource`).
/// On a sphere the centre is a longitude and latitude, and cells are `pixelSize` metres along a
/// meridian, the same in degrees of longitude.
struct FlatSourceSpec {
    std::size_t width = 513;
    std::size_t height = 513;
    double pixelSize = 2.0;
    float elevation = 0.0f;
    double centreX = 0.0;
    double centreY = 0.0;

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
struct MarsSurfaceSpec {  ///< "mars_surface": `MarsSurface(ageGyr)`, density and ages.
    double ageGyr = 3.0;
    bool operator==(const MarsSurfaceSpec&) const = default;
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
using DensitySpec =
    std::variant<LunarSurfaceSpec, MarsSurfaceSpec, PowerLawSpec, PiecewisePowerLawSpec,
                 TabulatedSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec>;

/// Crater age models.
struct AutoAgesSpec {  ///< "auto": the lunar or Martian surface's ages, otherwise uniform 1 Gyr.
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
/// Where "diffusion" degradation takes its diffusion ages from.
enum class DiffusivityModel {
    Fassett2022Hartmann,  ///< "fassett_2022_hartmann": `Fassett2022HartmannDiffusivity`.
    Fassett2022Trask,     ///< "fassett_2022_trask": `Fassett2022TraskDiffusivity`.
    FassettThomson2014,   ///< "fassett_thomson_2014": `FassettThomson2014Diffusivity`.
    /// "fassett_thomson_2014_calibration": `FassettThomson2014DiffusionAge`.
    FassettThomson2014Calibration,
    PowerLaw,  ///< "power_law": the spec's `powerLaw`.
};

/// "diffusion": `DiffusionDegradation`, with the layer's profile worn by topographic diffusion
/// (`DiffusedProfile` reaching `reachRadii` radii). The lunar models are refused on Mars.
struct DiffusionDegradationSpec {
    DiffusivityModel diffusivity = DiffusivityModel::Fassett2022Hartmann;
    PowerLawDiffusivity powerLaw{};
    double reachRadii = 3.0;
    bool operator==(const DiffusionDegradationSpec&) const = default;
};
using DegradationSpec =
    std::variant<FreshnessDegradationSpec, NoDegradationSpec, DiffusionDegradationSpec>;

/// Crater profiles.
struct FassettThomsonSpec {  ///< "fassett_thomson": `FassettThomsonProfile(rim)`.
    FassettThomsonRim rim = FassettThomsonRim::Original;
    bool operator==(const FassettThomsonSpec&) const = default;
};
struct MartinParkesDunstanSpec {  ///< "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
    bool operator==(const MartinParkesDunstanSpec&) const = default;
};
using SimpleProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;
/// "complex": `simple` below the crater layer's body's simple-to-complex transition and complex
/// craters, with flat floors and central peaks, from it (`ComplexCraterProfile` with
/// `LunarComplexCraterShape` or `MarsComplexCraterShape`).
struct ComplexProfileSpec {
    SimpleProfileSpec simple{};
    bool operator==(const ComplexProfileSpec&) const = default;
};
using ProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec, ComplexProfileSpec>;

/// The body a crater layer's craters are shaped for: its fresh morphometry
/// (`LunarFreshCraterMorphometry`, `MarsFreshCraterMorphometry`) and complex crater shape.
/// Separate from `Body` because only these two bodies have crater morphometry.
enum class CraterBody { Moon, Mars };

/// "craters": craters generated over the grid and applied to it (`CraterLayer`). With `body` Mars,
/// the lunar-only density "lunar_surface" and ages "steady_state" are refused, and with `body` Moon
/// the density "mars_surface".
struct CraterLayerSpec {
    CraterBody body = CraterBody::Moon;
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

/// Boulder size distributions, named in JSON by their "type"; "power_law", "piecewise_power_law"
/// and "tabulated" are also accepted, as for crater densities.
struct GolombekRappBouldersSpec {  ///< "golombek_rapp": the layer's coverage, qa and qb.
    bool operator==(const GolombekRappBouldersSpec&) const = default;
};
struct Sp8023IntercraterBouldersSpec {  ///< "sp8023_intercrater": `Sp8023IntercraterBlocks(curve)`.
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023IntercraterBouldersSpec&) const = default;
};
struct Sp8023CraterBouldersSpec {  ///< "sp8023_crater": `Sp8023CraterBlocks(curve)`.
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023CraterBouldersSpec&) const = default;
};
struct SynthterrainBouldersSpec {  ///< "synthterrain": `SynthterrainBoulderDistribution(model)`.
    SynthterrainBoulders model = SynthterrainBoulders::InterCrater;
    bool operator==(const SynthterrainBouldersSpec&) const = default;
};
using BoulderSizesSpec =
    std::variant<GolombekRappBouldersSpec, Sp8023IntercraterBouldersSpec, Sp8023CraterBouldersSpec,
                 SynthterrainBouldersSpec, PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec>;

/// "boulders": boulders scattered over the terrain (`BoulderLayer`). Ejecta come from the nearest
/// crater layer before this one, which must exist when crater blocks are enabled. `sizes` and
/// `ejectaSizes` give `boulders.distribution` and `boulders.ejecta.distribution`; with
/// `extrapolate`, tables may be evaluated beyond their published diameters.
/// `boulders.cacheBoulders` is not part of the configuration.
struct BoulderLayerSpec {
    BoulderLayerOptions boulders{};
    BoulderSizesSpec sizes{};
    BoulderSizesSpec ejectaSizes{};
    bool extrapolate = false;

    bool operator==(const BoulderLayerSpec&) const = default;
};

/// "roughness": fine roughness of a given RMS slope (`RoughnessLayer`).
struct RoughnessLayerSpec {
    RoughnessOptions roughness{};

    bool operator==(const RoughnessLayerSpec&) const = default;
};

/// "bedforms": fields of wind-blown ripples or dunes (`BedformLayer`); angles in radians.
struct BedformLayerSpec {
    BedformOptions bedforms{};

    bool operator==(const BedformLayerSpec&) const = default;
};

/// "secondaries": secondary craters around the craters of the last crater layer before this one
/// (`SecondaryCraterLayer`), shaped with that layer's profile at a fixed depth ratio `depthRatio`
/// (0.11, the average Pike and Wilhelms (1978) found for simple lunar secondaries, as Singer et al.
/// (2020) report it) and the fresh rim of `body`. `secondaries.onsetCount` must be given.
struct SecondaryLayerSpec {
    SecondaryCraterOptions secondaries{};
    double depthRatio = 0.11;
    CraterBody body = CraterBody::Moon;

    bool operator==(const SecondaryLayerSpec&) const = default;
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

using LayerSpec = std::variant<NoiseLayerSpec, CraterLayerSpec, BoulderLayerSpec,
                               RoughnessLayerSpec, BedformLayerSpec, ScaleLayerSpec, ClampLayerSpec,
                               RegionLayerSpec, SecondaryLayerSpec>;

/// "region": `layer` applied only inside `shape`, feathered over `feather` metres outside it
/// (`RegionLayer`). Boulder ejecta do not see crater layers inside regions.
struct RegionLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};

    bool operator==(const RegionLayerSpec&) const = default;
};

/// Filters, named in JSON by their "type": "rim_smoothing" is `RimSmoothingFilter`.
using FilterSpec = std::variant<RimSmoothing>;

/// Formats for crater and boulder lists.
enum class ListFormat { Csv, Json };

/// A list file to write; the format follows the extension (.json or else CSV) unless given.
struct ListOutput {
    std::string path;
    ListFormat format = ListFormat::Csv;
    bool operator==(const ListOutput&) const = default;
};

/// A float32 GeoTIFF file to write, with a raster source's projection.
struct GeoTiffOutput {
    std::string path;
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;
    bool operator==(const GeoTiffOutput&) const = default;
};

/// "crater_list": writes the generated and manual craters.
struct CraterListOutput : ListOutput {};

/// "heightmap": writes the generated heights.
struct HeightmapOutput : GeoTiffOutput {};

/// "boulder_list": writes every boulder the layers placed, models and relief alike.
struct BoulderListOutput : ListOutput {};

/// "boulder_models": writes the boulders placed as 3D models (`BoulderModelThreshold`) on the
/// ground beneath them, as a glTF scene (.glb) drawing each shared shape once per boulder, or as
/// one merged PLY mesh (.ply). Vertices are split where faces meet at more than `creaseAngle`
/// degrees.
struct BoulderModelsOutput {
    std::string path;
    double creaseAngle = 35.0;
    bool operator==(const BoulderModelsOutput&) const = default;
};

/// "albedo": writes the albedo (`TerrainConfig::albedo`) with one band, or three (red, green,
/// blue).
struct AlbedoOutput : GeoTiffOutput {};

/// "body_mesh": a mesh of the whole body on a sphere (`BodyMesh`), `cellsPerFace` quads along each
/// edge of the six cube faces, sampled every `spacing` metres (by default a quad's side), as a
/// glTF binary (.glb, with smooth normals) or PLY (.ply).
struct BodyMeshOutput {
    std::string path;
    std::size_t cellsPerFace = 128;
    std::optional<double> spacing{};
    bool operator==(const BodyMeshOutput&) const = default;
};

using OutputSpec = std::variant<HeightmapOutput, CraterListOutput, BoulderListOutput,
                                BoulderModelsOutput, AlbedoOutput, BodyMeshOutput>;

/// Albedo sources, named in JSON by their "type".
///
/// "synthetic": `SyntheticAlbedo`, a base colour with a noise stack per channel (each 0 to
/// `kMaxNoiseLayers` layers), measured from the terrain source's reference point.
struct SyntheticAlbedoSpec {
    bool rgb = false;
    double hue = 0.0;
    double saturation = 0.0;
    double value = SyntheticAlbedoParams{}.value;
    std::vector<NoiseLayerConfig> hueNoise;
    std::vector<NoiseLayerConfig> saturationNoise;
    std::vector<NoiseLayerConfig> valueNoise;

    /// The parameters, with each noise layer's Hurst exponent applied.
    SyntheticAlbedoParams Params() const;

    bool operator==(const SyntheticAlbedoSpec&) const = default;
};

/// "raster": `RasterAlbedo` from one or three `bands` (from 0) of a GeoTIFF in the terrain's
/// projected coordinates, such as an orbital normal albedo map. Relative paths resolve against the
/// configuration's folder. Samples are scaled as the file's GDAL metadata says, or by `scale` and
/// `offset` when given, for maps stored as integers.
struct RasterAlbedoSpec {
    std::string path;
    std::vector<std::size_t> bands{0};
    std::optional<double> scale{};
    std::optional<double> offset{};
    RasterAlbedoOptions options{};

    bool operator==(const RasterAlbedoSpec&) const = default;
};

using AlbedoSourceSpec = std::variant<SyntheticAlbedoSpec, RasterAlbedoSpec>;

/// Albedo layers, named in JSON by their "type".
///
/// "craters": `CraterAlbedo`, bright ejecta and rays of the craters of the `craterLayer`th
/// top-level crater layer (from 0), or of every top-level crater layer when unset.
struct CraterAlbedoSpec {
    std::optional<std::size_t> craterLayer;
    CraterAlbedoOptions options{};

    bool operator==(const CraterAlbedoSpec&) const = default;
};

using AlbedoLayerSpec = std::variant<CraterAlbedoSpec>;

/// The albedo of the terrain: a source and the layers that change it, evaluated over the
/// terrain's grid.
struct AlbedoSpec {
    AlbedoSourceSpec source{};
    std::vector<AlbedoLayerSpec> layers;

    bool operator==(const AlbedoSpec&) const = default;
};

/// What a terrain lies on, named in JSON by its "type".
///
/// "plane": a projected frame in metres, the default.
struct PlaneSurfaceSpec {
    bool operator==(const PlaneSurfaceSpec&) const = default;
};

/// "sphere": a body of `radius` metres, such as a planet or moon; grids and positions are
/// longitude and latitude in degrees, and features are generated over the whole body (`Surface`).
/// Give the radius, or a `body` ("moon", "mars", "mercury") for its mean radius (`MeanRadius`),
/// not both.
struct SphereSurfaceSpec {
    double radius = 0.0;
    std::optional<Body> body{};
    bool operator==(const SphereSurfaceSpec&) const = default;
};

using SurfaceSpec = std::variant<PlaneSurfaceSpec, SphereSurfaceSpec>;

/// Everything needed to generate a terrain: the surface it lies on, a source, the layers added to
/// it in order, the filters applied after them in order, the outputs to write, and optionally its
/// albedo.
struct TerrainConfig {
    int schemaVersion = kConfigSchemaVersion;
    SurfaceSpec surface{};
    SourceSpec source{};
    std::vector<LayerSpec> layers;
    std::vector<FilterSpec> filters;
    std::vector<OutputSpec> outputs;
    std::optional<AlbedoSpec> albedo;  ///< None by default.

    bool operator==(const TerrainConfig&) const = default;
};

}  // namespace terraingen
