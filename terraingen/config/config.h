/// @file
/// @brief The configuration specs: what a terrain configuration holds, as plain structs.
#pragma once

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/distribution/viper.h"
#include "terraingen/filters/rim_smoothing.h"
#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/region.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/raster/geotiff/geotiff.h"
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

/// One noise layer, its octave gain optionally derived from a Hurst exponent.
struct NoiseLayerConfig {
    NoiseLayer layer{};
    /// When set, the octave gain comes from it (`GainFromHurst`) instead of `layer.fractal.gain`.
    std::optional<float> hurst;

    /// `layer` with its gain replaced from `hurst` when that is set.
    NoiseLayer Effective() const;

    bool operator==(const NoiseLayerConfig&) const = default;
};

/// Source "flat": a level datum over a `width` x `height` grid (`FlatSource`).
///
/// On a sphere the centre is a longitude and latitude, and cells are `pixelSize` metres along a
/// meridian, the same in degrees of longitude.
struct FlatSourceSpec {
    std::size_t width = 513;   ///< Cells.
    std::size_t height = 513;  ///< Cells.
    double pixelSize = 2.0;    ///< Metres.
    float elevation = 0.0f;    ///< The datum's height, in metres.
    double centreX = 0.0;      ///< Centre in surface coordinates.
    double centreY = 0.0;      ///< Centre in surface coordinates.

    bool operator==(const FlatSourceSpec&) const = default;
};

/// Source "raster": a heightmap read from a GeoTIFF, in its own projected coordinates
/// (`GeoTiffSource`).
///
/// Relative paths resolve against the configuration's folder. The raster can be resampled to
/// `pixelSize` metres over the same extent, as when upscaling a DEM; its coordinates must then be
/// a projection in metres.
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

/// A source, named in JSON by its "type".
using SourceSpec = std::variant<FlatSourceSpec, RasterSourceSpec, ChainSourceSpec>;

/// Source "chain": `sources` tried in order, each filling where those before it have no
/// data (`SourceChain`); the terrain covers the first source's grid.
struct ChainSourceSpec {
    std::vector<SourceSpec> sources;

    bool operator==(const ChainSourceSpec&) const = default;
};

/// Layer "noise": a stack of 1 to `kMaxNoiseLayers` noise layers added to the terrain
/// (`NoiseRelief`), measured from the source's reference point.
struct NoiseLayerSpec {
    std::vector<NoiseLayerConfig> stack{NoiseLayerConfig{}};

    /// The stack as terrain parameters, with each layer's Hurst exponent applied.
    SyntheticTerrainParams Params() const;

    bool operator==(const NoiseLayerSpec&) const = default;
};

/// Density "power_law": `PowerLawSfd`, N per m^2 with D in metres.
struct PowerLawSpec {
    double coefficient = 0.1;
    double exponent = 2.0;
    bool operator==(const PowerLawSpec&) const = default;
};
/// Density "piecewise_power_law": `PiecewisePowerLawSfd`.
struct PiecewisePowerLawSpec {
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
    bool operator==(const PiecewisePowerLawSpec&) const = default;
};
/// Density "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
struct TabulatedSpec {
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Metres per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square metres per table area unit; 1e6 for km^2.
    bool operator==(const TabulatedSpec&) const = default;
};
/// Crater density "trask": `TraskEquilibrium`.
struct TraskSpec {
    bool operator==(const TraskSpec&) const = default;
};
/// Crater density "hartmann": `HartmannEquilibrium`.
struct HartmannSpec {
    bool operator==(const HartmannSpec&) const = default;
};
/// Crater density "moore": `MooreDistribution(terrain)`.
struct MooreSpec {
    MooreTerrain terrain = MooreTerrain::RoughMare;
    bool operator==(const MooreSpec&) const = default;
};
/// Crater density "viper": `ViperSpecification`.
struct ViperSpec {
    bool operator==(const ViperSpec&) const = default;
};
/// A published crater production function, a `ProductionRate`.
enum class ProductionFunction {
    Neukum,          ///< "neukum": `NeukumProductionFunction`, 10 m to 300 km.
    ExtendedNeukum,  ///< "extended_neukum": `ExtendedNeukumProductionFunction`, 2.5 m to 300 km.
    Ivanov,          ///< "ivanov": `IvanovMarsProductionFunction`, 15 m to 362 km.
};
/// A published crater chronology, N(>=1 km) by surface age.
enum class ChronologyModel {
    Neukum,  ///< "neukum": `NeukumLunarChronology`.
    Ivanov,  ///< "ivanov": `IvanovMarsChronology`.
};
/// Crater density "production_surface": `ProductionSurface`, the craters `production`
/// leaves on a surface `ageGyr` old by `chronology`, with no erosion.
struct ProductionSurfaceSpec {
    ProductionFunction production = ProductionFunction::ExtendedNeukum;
    ChronologyModel chronology = ChronologyModel::Neukum;
    double ageGyr = 3.5;
    bool operator==(const ProductionSurfaceSpec&) const = default;
};
struct MinimumSpec;
/// A crater density, named in JSON by its "type".
using DensitySpec =
    std::variant<PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec, ProductionSurfaceSpec,
                 MinimumSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec>;
/// Crater density "minimum": `MinimumSfd`, the least of `distributions` at each diameter,
/// such as a production surface capped by an equilibrium.
struct MinimumSpec {
    std::vector<DensitySpec> distributions;
    bool operator==(const MinimumSpec&) const = default;
};

/// Crater ages "fresh": `FreshAges`.
struct FreshAgesSpec {
    bool operator==(const FreshAgesSpec&) const = default;
};
/// Crater ages "uniform": `UniformAges(lifetimeGyr)`.
struct UniformAgesSpec {
    double lifetimeGyr = 1.0;
    bool operator==(const UniformAgesSpec&) const = default;
};
/// Equilibrium densities for `SteadyStateAgesSpec`.
enum class EquilibriumModel { Trask, Hartmann };
/// Crater ages "steady_state": `SteadyStateAges` from `production`, `equilibrium` and
/// `chronology`.
struct SteadyStateAgesSpec {
    double ageGyr = 3.5;
    ProductionFunction production = ProductionFunction::ExtendedNeukum;
    EquilibriumModel equilibrium = EquilibriumModel::Trask;
    ChronologyModel chronology = ChronologyModel::Neukum;
    bool operator==(const SteadyStateAgesSpec&) const = default;
};
/// A crater age model, named in JSON by its "type".
using AgesSpec = std::variant<UniformAgesSpec, FreshAgesSpec, SteadyStateAgesSpec>;

/// Degradation "none": `NoDegradation`, optionally with a fixed depth-to-diameter ratio.
struct NoDegradationSpec {
    std::optional<double> depthRatio;
    bool operator==(const NoDegradationSpec&) const = default;
};
/// Degradation "freshness": `FreshnessDegradation`.
struct FreshnessDegradationSpec {
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

/// Degradation "diffusion": `DiffusionDegradation`, the layer's profile worn by topographic
/// diffusion (`DiffusedProfile`).
struct DiffusionDegradationSpec {
    DiffusivityModel diffusivity = DiffusivityModel::Fassett2022Hartmann;
    PowerLawDiffusivity powerLaw{};  ///< Used when `diffusivity` is `PowerLaw`.
    double reachRadii = 3.0;         ///< How far the diffused profile reaches, in crater radii.
    bool operator==(const DiffusionDegradationSpec&) const = default;
};
/// A degradation model, named in JSON by its "type".
using DegradationSpec =
    std::variant<FreshnessDegradationSpec, NoDegradationSpec, DiffusionDegradationSpec>;

/// Crater profile "fassett_thomson": `FassettThomsonProfile(rim)`.
struct FassettThomsonSpec {
    FassettThomsonRim rim = FassettThomsonRim::Original;
    bool operator==(const FassettThomsonSpec&) const = default;
};
/// Crater profile "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
struct MartinParkesDunstanSpec {
    bool operator==(const MartinParkesDunstanSpec&) const = default;
};
/// A simple crater profile, named in JSON by its "type".
using SimpleProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;
/// Crater profile "complex": `simple` below the simple-to-complex transition of the layer's
/// morphometry, and complex craters, with flat floors and central peaks, from it
/// (`ComplexCraterProfile` with `LunarComplexCraterShape` or `MarsComplexCraterShape`).
struct ComplexProfileSpec {
    SimpleProfileSpec simple{};
    bool operator==(const ComplexProfileSpec&) const = default;
};
/// A crater profile, named in JSON by its "type".
using ProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec, ComplexProfileSpec>;

/// The published fits of fresh crater depth, rim height and complex crater shape that a
/// crater layer shapes its craters with.
enum class CraterMorphometryFit {
    Lunar,  ///< "lunar": `LunarFreshCraterMorphometry` and `LunarComplexCraterShape`.
    Mars,   ///< "mars": `MarsFreshCraterMorphometry` and `MarsComplexCraterShape`.
};

/// Layer "craters": craters generated over the grid and applied to it (`CraterLayer`).
struct CraterLayerSpec {
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;
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

/// Boulder sizes "golombek_rapp": the layer's coverage, qa and qb.
struct GolombekRappBouldersSpec {
    bool operator==(const GolombekRappBouldersSpec&) const = default;
};
/// Boulder sizes "sp8023_intercrater": `Sp8023IntercraterBlocks(curve)`.
struct Sp8023IntercraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023IntercraterBouldersSpec&) const = default;
};
/// Boulder sizes "sp8023_crater": `Sp8023CraterBlocks(curve)`.
struct Sp8023CraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023CraterBouldersSpec&) const = default;
};
/// Boulder sizes "synthterrain": `SynthterrainBoulderDistribution(model)`.
struct SynthterrainBouldersSpec {
    SynthterrainBoulders model = SynthterrainBoulders::InterCrater;
    bool operator==(const SynthterrainBouldersSpec&) const = default;
};
/// A boulder size distribution, named in JSON by its "type"; "power_law",
/// "piecewise_power_law" and "tabulated" are as for crater densities.
using BoulderSizesSpec =
    std::variant<GolombekRappBouldersSpec, Sp8023IntercraterBouldersSpec, Sp8023CraterBouldersSpec,
                 SynthterrainBouldersSpec, PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec>;

/// Layer "boulders": boulders scattered over the terrain (`BoulderLayer`).
///
/// Ejecta come from the nearest crater layer before this one, which must exist when crater blocks
/// are enabled. `boulders.cacheBoulders` is not part of the configuration.
struct BoulderLayerSpec {
    BoulderLayerOptions boulders{};
    BoulderSizesSpec sizes{};        ///< Gives `boulders.sizes.distribution`.
    BoulderSizesSpec ejectaSizes{};  ///< Gives `boulders.ejecta.distribution`.
    bool extrapolate = false;        ///< Let tables be evaluated beyond their published diameters.
    /// Rock meshes (.obj, .gltf or .glb) that replace the procedural shapes, as
    /// `BoulderShapeOptions::library`; relative to the configuration's folder.
    std::vector<std::string> shapeFiles{};

    bool operator==(const BoulderLayerSpec&) const = default;
};

/// Layer "roughness": fine roughness of a given RMS slope (`RoughnessLayer`).
struct RoughnessLayerSpec {
    RoughnessOptions roughness{};

    bool operator==(const RoughnessLayerSpec&) const = default;
};

/// Layer "bedforms": fields of wind-blown ripples or dunes (`BedformLayer`); angles in
/// radians.
struct BedformLayerSpec {
    BedformOptions bedforms{};

    bool operator==(const BedformLayerSpec&) const = default;
};

/// Layer "secondaries": secondary craters around the craters of the last crater layer
/// before this one (`SecondaryCraterLayer`), shaped with that layer's profile at a fixed depth
/// ratio and the fresh rim of `morphometry`. `secondaries.onsetCount` must be given.
struct SecondaryLayerSpec {
    SecondaryCraterOptions secondaries{};
    /// Depth-to-diameter ratio.
    double depthRatio = 0.11;
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;

    bool operator==(const SecondaryLayerSpec&) const = default;
};

/// Layer "scale": height * `scale` + `offset` (`ScaleLayer`).
struct ScaleLayerSpec {
    double scale = 1.0;
    double offset = 0.0;  ///< Metres.

    bool operator==(const ScaleLayerSpec&) const = default;
};

/// Layer "clamp": heights clamped to [`min`, `max`], either optional (`ClampLayer`).
struct ClampLayerSpec {
    std::optional<double> min;  ///< Metres; unset for no lower bound.
    std::optional<double> max;  ///< Metres; unset for no upper bound.

    bool operator==(const ClampLayerSpec&) const = default;
};

/// Region shape "rectangle": [`minX`, `maxX`] x [`minY`, `maxY`] in surface coordinates.
struct RectangleRegionSpec {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool operator==(const RectangleRegionSpec&) const = default;
};
/// Region shape "circle": centred on (`x`, `y`) in surface coordinates.
struct CircleRegionSpec {
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;  ///< Metres.
    bool operator==(const CircleRegionSpec&) const = default;
};
/// A region shape, named in JSON by its "type".
using RegionShapeSpec = std::variant<RectangleRegionSpec, CircleRegionSpec>;

/// A value held by pointer, so that a spec can hold the variant it belongs to; it is copied
/// and compared by value.
template <typename T> class Boxed {
  public:
    /// A default-constructed value.
    Boxed() : value_(std::make_unique<T>()) {}
    /// A value made from anything `T` can be made from, such as one alternative of a variant.
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

/// A layer, named in JSON by its "type".
using LayerSpec = std::variant<NoiseLayerSpec, CraterLayerSpec, BoulderLayerSpec,
                               RoughnessLayerSpec, BedformLayerSpec, ScaleLayerSpec, ClampLayerSpec,
                               RegionLayerSpec, SecondaryLayerSpec>;

/// Layer "region": `layer` applied only inside `shape` (`RegionLayer`). Boulder ejecta do
/// not see crater layers inside regions.
struct RegionLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;  ///< Metres outside the shape over which the layer fades out.
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};

    bool operator==(const RegionLayerSpec&) const = default;
};

/// A filter, named in JSON by its "type": "rim_smoothing" is `RimSmoothingFilter`.
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
    std::uint32_t tileSize = 256;                        ///< Pixels per tile side.
    bool operator==(const GeoTiffOutput&) const = default;
};

/// Output "crater_list": writes the generated and manual craters.
struct CraterListOutput : ListOutput {};

/// Output "heightmap": writes the generated heights.
struct HeightmapOutput : GeoTiffOutput {};

/// Output "boulder_list": writes every boulder the layers placed, models and relief alike.
struct BoulderListOutput : ListOutput {};

/// Output "boulder_models": writes the boulders placed as 3D models
/// (`BoulderModelThreshold`) on the ground beneath them, as `WriteBoulderModels` does.
struct BoulderModelsOutput {
    std::string path;
    double creaseAngle = 35.0;  ///< Degrees between faces above which vertices are split.
    bool operator==(const BoulderModelsOutput&) const = default;
};

/// Output "albedo": writes the albedo (`TerrainConfig::albedo`) with one band, or three
/// (red, green, blue).
struct AlbedoOutput : GeoTiffOutput {};

/// Output "body_mesh": a mesh of the whole body on a sphere (`BodyMesh`), with smooth normals, in
/// any format `WriteMesh` writes.
struct BodyMeshOutput {
    std::string path;
    std::size_t cellsPerFace = 128;   ///< Quads along each edge of the six cube faces.
    std::optional<double> spacing{};  ///< Sample spacing in metres; unset for a quad's side.
    bool operator==(const BodyMeshOutput&) const = default;
};

/// Output "terrain_mesh": the generated heights as a mesh (`HeightMesh`), with smooth normals, in
/// any format `WriteMesh` writes and in the frame boulder models are written in.
struct TerrainMeshOutput {
    std::string path;
    bool operator==(const TerrainMeshOutput&) const = default;
};

/// An output, named in JSON by its "type".
using OutputSpec =
    std::variant<HeightmapOutput, CraterListOutput, BoulderListOutput, BoulderModelsOutput,
                 AlbedoOutput, BodyMeshOutput, TerrainMeshOutput>;

/// Albedo source "synthetic": `SyntheticAlbedo`, a base colour with a noise stack per
/// channel (each 0 to `kMaxNoiseLayers` layers), measured from the terrain source's reference
/// point.
struct SyntheticAlbedoSpec {
    bool rgb = false;
    double hue = 0.0;
    double saturation = 0.0;
    double value = SyntheticAlbedoParams{}.value;
    std::vector<NoiseLayerConfig> hueNoise;
    std::vector<NoiseLayerConfig> saturationNoise;
    std::vector<NoiseLayerConfig> valueNoise;

    /// The albedo parameters, with each noise layer's Hurst exponent applied.
    SyntheticAlbedoParams Params() const;

    bool operator==(const SyntheticAlbedoSpec&) const = default;
};

/// Albedo source "raster": `RasterAlbedo` from one or three bands of a GeoTIFF in the
/// terrain's projected coordinates, such as an orbital normal albedo map.
///
/// Relative paths resolve against the configuration's folder. Samples are scaled as the file's
/// GDAL metadata says, or by `scale` and `offset` when given, for maps stored as integers.
struct RasterAlbedoSpec {
    std::string path;
    std::vector<std::size_t> bands{0};  ///< One or three bands, from 0.
    std::optional<double> scale{};
    std::optional<double> offset{};
    RasterAlbedoOptions options{};

    bool operator==(const RasterAlbedoSpec&) const = default;
};

/// An albedo source, named in JSON by its "type".
using AlbedoSourceSpec = std::variant<SyntheticAlbedoSpec, RasterAlbedoSpec>;

/// Albedo layer "craters": `CraterAlbedo`, bright ejecta and rays of the craters of one
/// top-level crater layer, or of every one.
struct CraterAlbedoSpec {
    /// Index of the top-level crater layer, from 0; unset for every crater layer.
    std::optional<std::size_t> craterLayer;
    CraterAlbedoOptions options{};

    bool operator==(const CraterAlbedoSpec&) const = default;
};

/// An albedo layer, named in JSON by its "type".
using AlbedoLayerSpec = std::variant<CraterAlbedoSpec>;

/// The albedo of the terrain: a source and the layers that change it, evaluated over the
/// terrain's grid.
struct AlbedoSpec {
    AlbedoSourceSpec source{};
    std::vector<AlbedoLayerSpec> layers;

    bool operator==(const AlbedoSpec&) const = default;
};

/// Surface "plane": a projected frame in metres, the default.
struct PlaneSurfaceSpec {
    bool operator==(const PlaneSurfaceSpec&) const = default;
};

/// Surface "sphere": a body such as a planet or moon (`Surface`); grids and positions are
/// longitude and latitude in degrees, and features are generated over the whole body.
struct SphereSurfaceSpec {
    double radius = 0.0;  ///< Metres; must be given.
    bool operator==(const SphereSurfaceSpec&) const = default;
};

/// What a terrain lies on, named in JSON by its "type".
using SurfaceSpec = std::variant<PlaneSurfaceSpec, SphereSurfaceSpec>;

/// Everything needed to generate a terrain: the surface it lies on, a source, the layers
/// added to it in order, the filters applied after them in order, the outputs to write, and
/// optionally its albedo.
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
