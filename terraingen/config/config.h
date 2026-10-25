// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The configuration specs: what a terrain configuration holds, as plain structs.
#pragma once

#include "terraingen/albedo/blast_zones.h"
#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/photometry.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/rock_albedo.h"
#include "terraingen/albedo/slope_albedo.h"
#include "terraingen/albedo/splotches.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/distribution/piecewise_uniform.h"
#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/distribution/viper.h"
#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/masked.h"
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/roughness_estimate.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/sand_shadow.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/layers/strata.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/pipeline/feature_lists.h"
#include "terraingen/pipeline/materials.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

/// Smallest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMinConfigGridSize = 2;

/// Largest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// Largest number of cells along a cube face edge of a body mesh or a shape model's radial grid.
inline constexpr std::size_t kMaxCubeFaceCells = 4096;

/// What GeoTIFF output tile sides, in pixels, must be a multiple of.
inline constexpr std::uint32_t kTileSizeMultiple = 16;

/// Amplitude over wavelength of a terrain noise layer whose amplitude is unset, near the lunar
/// highland median slope (Rosenburg et al. 2011, https://doi.org/10.1029/2010JE003716).
inline constexpr float kNoiseAmplitudeRatio = 0.1f;

/// One noise layer, its octave gain optionally derived from a Hurst exponent.
struct NoiseStackLayerSpec {
    NoiseLayer layer{};
    /// When set, the octave gain comes from it (`GainFromHurst`) instead of `layer.fractal.gain`.
    std::optional<float> hurst;
    /// When set, the amplitude in place of `layer.fractal.amplitude`; unset gives terrain noise
    /// `kNoiseAmplitudeRatio` times its wavelength (`EffectiveRelief`).
    std::optional<float> amplitude;

    /// `layer` with its gain replaced from `hurst` and its amplitude from `amplitude` when set.
    NoiseLayer Effective() const;
    /// `Effective` with an unset amplitude `kNoiseAmplitudeRatio` times the wavelength, as terrain
    /// noise layers take it.
    NoiseLayer EffectiveRelief() const;
};

/// One layer of an albedo noise stack: a noise layer whose amplitude is in what the stack varies,
/// relative brightness, saturation or degrees of hue, rather than in meters, and required.
struct AlbedoNoiseLayerSpec : NoiseStackLayerSpec {};

/// How a source's posts become heights between them and at sample spacings finer than them
/// (`Upsampler`): `type` names a kernel upsampler (`KernelUpsamplerNames`), "diamond_square", or a
/// type registered with `RegisterUpsampler`. In JSON a name alone, or an object with the "type"
/// and the type's settings.
struct UpsamplingSpec {
    std::string type = "cubic";
    DiamondSquareOptions diamondSquare{};  ///< The settings of "diamond_square"; meters.
    /// A registered type's settings, a JSON object as text without the "type"; empty for none.
    std::string settings{};
};

/// Source "raster": a heightmap read from a GeoTIFF, in its own projected coordinates
/// (`GeoTiffSource`), upsampled as `upsampling` says.
///
/// Relative paths resolve against the configuration's folder.
struct RasterSourceSpec {
    std::string path;
    /// How the band is read: band, scale, offset, crop and `effectivePixelSize` (meters); its
    /// upsampler must stay unset, as `upsampling` gives it.
    GeoTiffSourceOptions read{};
    UpsamplingSpec upsampling{};
};

/// The terrain parameters of a stack of noise layers: each layer's Hurst exponent applied and an
/// unset amplitude `kNoiseAmplitudeRatio` times its wavelength
/// (`NoiseStackLayerSpec::EffectiveRelief`).
SyntheticTerrainParams StackParams(const std::vector<NoiseStackLayerSpec>& stack);

/// The noise layer a synthetic source starts from by default: fBm 400 m in wavelength and 12 m in
/// amplitude, seed 2, the `FractalParams` defaults otherwise; rolling ground that craters and
/// boulders look natural on at a meter per pixel.
inline NoiseStackLayerSpec DefaultBaseNoise() {
    NoiseStackLayerSpec base;
    base.layer.fractal.seed = 2;
    base.layer.fractal.wavelength = 400.0f;
    base.layer.fractal.amplitude = 12.0f;
    base.amplitude = 12.0f;
    return base;
}

/// Source "synthetic": `elevation` plus a stack of noise layers (`SyntheticSource`), evaluated at
/// every point, or with `pixelSize` sampled at posts that `upsampling` refines (cubic unset).
struct SyntheticSourceSpec {
    double elevation = 0.0;  ///< Meters.
    /// 0 to `kMaxNoiseLayers` layers, by default `DefaultBaseNoise`; an empty stack is flat.
    std::vector<NoiseStackLayerSpec> stack{DefaultBaseNoise()};
    std::optional<double> pixelSize{};           ///< Meters between posts; unset for none.
    std::optional<UpsamplingSpec> upsampling{};  ///< Needs `pixelSize`.
};

/// A scalar field over the surface (`SurfaceField`): a constant `value`, or a GeoTIFF band read
/// in the surface's coordinates (`RasterField`) when `path` is set.
struct FieldSpec {
    std::string path{};    ///< GeoTIFF, relative to the configuration's folder; empty for `value`.
    double value = 0.0;    ///< The field everywhere, without a `path`.
    std::size_t band = 0;  ///< The GeoTIFF band, from 0.
    ResampleMethod method = ResampleMethod::Bilinear;
    double scale = 1.0;                ///< Multiplies every cell.
    std::optional<double> fallback{};  ///< Where the GeoTIFF has no value; unset leaves none.
    bool periodicLongitude = false;    ///< The GeoTIFF's x is longitude repeating every 360 deg.
};

/// A density map (`DensityMap`) of a crater or boulder population.
struct DensityMapSpec {
    FieldSpec field{};
    DensityMeasure measure = DensityMeasure::Scale;
    double minDiameter = 0.0;             ///< Meters.
    std::optional<double> maxDiameter{};  ///< Meters; unset for no upper limit.
    double areaUnit = 1.0;                ///< Square meters per map area unit.
    bool total = true;
};

/// A size slope map (`SizeSlopeMap`) of a boulder population.
struct SizeSlopeMapSpec {
    FieldSpec field{};
    SizeSlopeMeasure measure = SizeSlopeMeasure::Exponent;
    double referenceDiameter = 1.0;  ///< Meters.
    double minExponent = 1.0;
    double maxExponent = 6.0;
};

struct ChainSourceSpec;

/// Where a terrain's heights come from, its source, named in JSON by its "type".
using SourceSpec = std::variant<RasterSourceSpec, ChainSourceSpec, SyntheticSourceSpec>;

/// Source "chain": `sources` tried in order, each filling where those before it have no
/// data (`SourceChain`), joined as `seams` say; its grid is the first source's.
struct ChainSourceSpec {
    std::vector<SourceSpec> sources;
    ChainSeams seams{};  ///< `width` and `decayLength` in meters.
};

/// Layer "noise": a stack of 1 to `kMaxNoiseLayers` noise layers added to the terrain
/// (`NoiseRelief`), measured from the source's reference point.
struct NoiseLayerSpec {
    std::vector<NoiseStackLayerSpec> stack{NoiseStackLayerSpec{}};
    /// Whether unset Hurst exponents and amplitudes come from the source (`EstimatedParams` of
    /// `EstimateSourceRoughness`); needs a source.
    bool estimateFromSource = false;
    /// Source cells across from which the source's measured data holds a noise layer: each layer
    /// whose wavelength, its longest, is at least that many source cells at a point is left out
    /// there (`NoiseRelief`). Ignored on a synthetic source; unset adds every layer everywhere.
    std::optional<double> sourceResolvesPixels{};

    /// The stack as terrain parameters, with each layer's Hurst exponent applied.
    SyntheticTerrainParams Params() const;

    /// `Params` with each layer's unset Hurst exponent `source.hurst` and unset amplitude the one
    /// that continues `source` at the layer's Hurst exponent to a lag of its wavelength
    /// (`MatchedNoiseAmplitude`).
    /// @throws std::invalid_argument as `MatchedNoiseAmplitude`.
    SyntheticTerrainParams EstimatedParams(const BaseRoughness& source) const;
};

/// Density "power_law": `PowerLawSfd`, N per m^2 with D in meters.
struct PowerLawSpec {
    double coefficient = 0.1;
    double exponent = 2.0;
};
/// Density "piecewise_power_law": `PiecewisePowerLawSfd`.
struct PiecewisePowerLawSpec {
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
};
/// Density "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
struct TabulatedSpec {
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Meters per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square meters per table area unit; 1e6 for km^2.
};
/// Density "piecewise_uniform": `PiecewiseUniformSfd`, bands with diameters spread uniformly.
struct PiecewiseUniformSpec {
    std::vector<UniformBand> bands{UniformBand{}};
    double total = 0.0;  ///< Per m^2 across all bands, shared by `density`; 0 for as given.
};
/// Crater density "trask": `TraskEquilibrium`.
struct TraskSpec {};
/// Crater density "hartmann": `HartmannEquilibrium`.
struct HartmannSpec {};
/// Crater density "moore": `MooreDistribution(terrain)`.
struct MooreSpec {
    MooreTerrain terrain = MooreTerrain::RoughMare;
};
/// Crater density "viper": `ViperSpecification`.
struct ViperSpec {};
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
};
struct MinimumSpec;
/// A crater density, named in JSON by its "type".
using DensitySpec =
    std::variant<PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec, ProductionSurfaceSpec,
                 MinimumSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec, PiecewiseUniformSpec>;
/// Crater density "minimum": `MinimumSfd`, the least of `distributions` at each diameter,
/// such as a production surface capped by an equilibrium.
struct MinimumSpec {
    std::vector<DensitySpec> distributions;
};

/// Crater ages "fresh": `FreshAges`.
struct FreshAgesSpec {};
/// Crater ages "uniform": `UniformAges(lifetimeGyr)`.
struct UniformAgesSpec {
    double lifetimeGyr = 1.0;
};
/// Equilibrium densities for `SteadyStateAgesSpec`.
enum class EquilibriumModel { Trask, Hartmann };
/// Crater ages "steady_state": `SteadyStateAges` from `production`, `equilibrium` and
/// `chronology`.
struct SteadyStateAgesSpec {
    double ageGyr = 3.5;
    ProductionFunction production = ProductionFunction::ExtendedNeukum;
    EquilibriumModel equilibrium = EquilibriumModel::Hartmann;
    ChronologyModel chronology = ChronologyModel::Neukum;
};
/// Crater ages "freshness": `FreshnessAges(freshness, lifetimeGyr)`.
struct FreshnessAgesSpec {
    double lifetimeGyr = 1.0;
    ValueDistribution freshness{ValueDistribution::Kind::Uniform, 0.0, 1.0, 0.0, 1.0};
};
/// A crater age model, named in JSON by its "type".
using AgesSpec =
    std::variant<UniformAgesSpec, FreshAgesSpec, SteadyStateAgesSpec, FreshnessAgesSpec>;

/// Degradation "none": `NoDegradation`, optionally with a fixed depth-to-diameter ratio.
struct NoDegradationSpec {
    std::optional<double> depthRatio;
};
/// Degradation "freshness": `FreshnessDegradation`.
struct FreshnessDegradationSpec {
    double cutoffDepthRatio = 0.04;
};
/// Where "diffusion" degradation takes its diffusion ages from.
enum class DiffusivityModel {
    /// "matched": `EquilibriumDiffusionAges` with steady-state ages, else `Fassett2022Hartmann`.
    Matched,
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
    DiffusivityModel diffusivity = DiffusivityModel::Matched;
    PowerLawDiffusivity powerLaw{};  ///< Used when `diffusivity` is `PowerLaw`.
    double reachRadii = 3.0;         ///< How far the diffused profile reaches, in crater radii.
};
/// A degradation model, named in JSON by its "type".
using DegradationSpec =
    std::variant<FreshnessDegradationSpec, NoDegradationSpec, DiffusionDegradationSpec>;

/// Crater profile "fassett_thomson": `FassettThomsonProfile(rim)`.
struct FassettThomsonSpec {
    FassettThomsonRim rim = FassettThomsonRim::Original;
};
/// Crater profile "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
struct MartinParkesDunstanSpec {};
/// A simple crater profile, named in JSON by its "type".
using SimpleProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;
/// Crater profile "complex": `simple` below the simple-to-complex transition of the layer's
/// morphometry, and complex craters, with flat floors and central peaks, from it
/// (`ComplexCraterProfile` with `LunarComplexCraterShape` or `MarsComplexCraterShape`).
struct ComplexProfileSpec {
    SimpleProfileSpec simple{MartinParkesDunstanSpec{}};
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
    /// Draws random craters from `density`; off places only the `manual` craters.
    bool generateRandom = true;
    std::uint32_t seed = 1;
    /// By default a 3.5 Gyr extended Neukum production surface capped by Hartmann equilibrium
    /// (Fassett 2022, https://doi.org/10.1029/2022JE007510).
    DensitySpec density{MinimumSpec{{ProductionSurfaceSpec{}, HartmannSpec{}}}};
    /// Unset gives steady-state ages matching the density (`LayerAges`).
    std::optional<AgesSpec> ages{};
    /// Meters; by default 2.5, where the extended Neukum production function begins.
    double minDiameter = 2.5;
    double maxDiameter = 250.0;  ///< Meters.
    CraterSpacing spacing{};
    std::optional<std::size_t>
        maxCraters{};  ///< Keep at most this many generated craters; unset keeps all.
    /// Manual craters, in the surface's coordinates; parsing sets their `manual` flag.
    std::vector<Crater> manual{};
    ProfileSpec profile{MartinParkesDunstanSpec{}};
    /// By default diffusion matched to those ages.
    DegradationSpec degradation{DiffusionDegradationSpec{}};
    CraterApplyOptions apply{DefaultCraterApply()};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `CraterLayerOptions::densityMap`.
    /// Gives `generation.completeness`: where `manual` is a complete catalog, from which diameter.
    std::optional<FieldSpec> completeness{};
    /// Gives `CraterLayerOptions::sourceResolvesPixels`: source cells across from which the
    /// source's measured data holds a crater, so only smaller craters are generated there, the
    /// minimum with `completeness` when both are set. Ignored on a synthetic source and where the
    /// source has no measured data; unset generates every crater.
    std::optional<double> sourceResolvesPixels = 4.0;
};

/// Boulder sizes "golombek_rapp": the layer's coverage, qa and qb.
struct GolombekRappBouldersSpec {};
/// Boulder sizes "sp8023_intercrater": `Sp8023IntercraterBlocks(curve)`.
struct Sp8023IntercraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
};
/// Boulder sizes "sp8023_crater": `Sp8023CraterBlocks(curve)`.
struct Sp8023CraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
};
/// Boulder sizes "synthterrain": `SynthterrainBoulderDistribution(model)`.
struct SynthterrainBouldersSpec {
    SynthterrainBoulders model = SynthterrainBoulders::InterCrater;
};
/// A boulder size distribution, named in JSON by its "type"; "power_law",
/// "piecewise_power_law", "tabulated" and "piecewise_uniform" are as for crater densities.
using BoulderSizesSpec =
    std::variant<GolombekRappBouldersSpec, Sp8023IntercraterBouldersSpec, Sp8023CraterBouldersSpec,
                 SynthterrainBouldersSpec, PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec,
                 PiecewiseUniformSpec>;

/// Layer "boulders": boulders scattered over the terrain (`BoulderLayer`).
///
/// Ejecta come from every top-level crater layer before this one, at least one of which must exist
/// when crater blocks are enabled. `boulders.cacheBoulders` is not part of the configuration.
struct BoulderLayerSpec {
    BoulderLayerOptions boulders{};
    /// Gives `boulders.sizes.distribution`; by default the nominal NASA SP-8023 intercrater blocks
    /// (SLS-SPEC-159 Rev. I, 3.4.1.4).
    BoulderSizesSpec sizes{Sp8023IntercraterBouldersSpec{SpecificationCurve::Nominal}};
    /// Gives `boulders.ejecta.distribution`; unset follows `sizes`.
    std::optional<BoulderSizesSpec> ejectaSizes{};
    bool extrapolate = false;  ///< Let tables be evaluated beyond their published diameters.
    /// Rock meshes (.obj, .gltf or .glb) that replace the procedural shapes, as
    /// `BoulderShapeOptions::library`; relative to the configuration's folder.
    std::vector<std::string> shapeFiles{};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `boulders.densityMap`.
    std::optional<SizeSlopeMapSpec> slopeMap{};  ///< Gives `boulders.slopeMap`.
    /// Catalog or hand-placed boulders in the surface's coordinates, as `boulders.manual`.
    std::vector<ManualBoulder> manual{};
    std::optional<FieldSpec> completeness{};  ///< Gives `boulders.completeness`.
};

/// Layer "roughness": fine roughness of a given RMS slope (`RoughnessLayer`), or of the RMS slope
/// and Hurst exponent the source's variogram continues to (`EstimateSourceRoughness`).
struct RoughnessLayerSpec {
    /// The roughness; its `rmsSlope` and `hurst` stand in for those below when they are unset and
    /// nothing is estimated.
    RoughnessOptions roughness{};
    std::optional<double> rmsSlope{};  ///< Rise over run; unset for the default or the estimate.
    std::optional<double> hurst{};     ///< Unset for the default or the estimate.
    /// Whether unset `rmsSlope` and `hurst` come from the source (`EstimatedOptions` of
    /// `EstimateSourceRoughness`); needs a source.
    bool estimateFromSource = false;

    /// `roughness` with `rmsSlope` and `hurst` in place where set.
    RoughnessOptions Options() const;

    /// `Options` with an unset `hurst` `source.hurst` and an unset `rmsSlope` the RMS slope over
    /// the baseline that continues `source` at the Hurst exponent (`BaseRoughness::RmsSlope`).
    RoughnessOptions EstimatedOptions(BaseRoughness source) const;
};

/// Layer "bedforms": fields of wind-blown ripples or dunes (`BedformLayer`); angles in
/// radians.
struct BedformLayerSpec {
    BedformOptions bedforms{};
};

/// Layer "dunes": barchans, domes or crest-line dunes with ripples on them (`DuneLayer`); angles
/// in radians.
struct DuneLayerSpec {
    DuneOptions dunes{};
};

/// Layer "pavement": fractured bedrock plates parted by cracks or ridges (`PavementLayer`);
/// angles in radians.
struct PavementLayerSpec {
    PavementOptions pavement{};
};

/// Layer "strata": the ground beneath terraced into beds of benches and cliffs (`StrataLayer`);
/// angles in radians.
struct StrataLayerSpec {
    StrataOptions strata{};
};

/// Layer "sand_shadow": sand tails in the lee of the boulders of the nearest boulder layer before
/// it (`SandShadowLayer`); angles in radians.
struct SandShadowLayerSpec {
    SandShadowOptions shadows{};
};

/// Layer "sand_fill": sand pooled in the lows of the ground beneath it (`SandFillLayer`).
struct SandFillLayerSpec {
    SandFillOptions sandFill{};
};

/// Layer "secondaries": secondary craters around the craters of the last crater layer
/// before this one (`SecondaryCraterLayer`), shaped with that layer's profile at a fixed depth
/// ratio and the fresh rim of `morphometry`. `secondaries.onsetCount` must be given.
struct SecondaryLayerSpec {
    SecondaryCraterOptions secondaries{};
    /// Depth-to-diameter ratio.
    double depthRatio = 0.11;
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;
};

/// Layer "spectral_continuation": detail below the source's resolution continuing its power-law
/// spectrum (`SpectralContinuationLayer`), fitted to the source (`EstimatePowerLawSpectrum`) on its
/// own grid wherever the slope or amplitude is not given.
struct SpectralContinuationLayerSpec {
    std::optional<double> pixelSize{};  ///< Meters; unset for the source's cell size.
    std::optional<double> slope{};      ///< Spectral slope beta; unset to fit from the source.
    std::optional<double> octaveRms{};  ///< Meters; unset to fit from the source.
    SpectrumFitBand fit{};              ///< Wavelengths the source's spectrum is fitted over.
    double minWavelength = 0.05;        ///< Meters.
    double taperOctaves = 1.0;
    std::uint32_t seed = 1;
    ResolutionFade fade{};

    /// The layer's options, the defaults in place of an unset pixel size, slope and amplitude.
    SpectralContinuationOptions Options() const;
};

/// Layer "scale": height * `scale` + `offset` (`ScaleLayer`).
struct ScaleLayerSpec {
    double scale = 1.0;
    double offset = 0.0;  ///< Meters.
};

/// Layer "clamp": heights clamped to [`min`, `max`], either optional (`ClampLayer`).
struct ClampLayerSpec {
    std::optional<double> min;  ///< Meters; unset for no lower bound.
    std::optional<double> max;  ///< Meters; unset for no upper bound.
};

/// Region shape "rectangle": [`minX`, `maxX`] x [`minY`, `maxY`] in surface coordinates.
struct RectangleRegionSpec {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
};
/// Region shape "circle": centered on (`x`, `y`) in surface coordinates.
struct CircleRegionSpec {
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;  ///< Meters.
};
/// Region shape "polygon": outlined by `points`, less `holes`, in surface coordinates.
struct PolygonRegionSpec {
    std::vector<std::array<double, 2>> points{};              ///< [x, y] of each vertex.
    std::vector<std::vector<std::array<double, 2>>> holes{};  ///< Each hole's vertices.
};
/// A region shape, named in JSON by its "type".
using RegionShapeSpec = std::variant<RectangleRegionSpec, CircleRegionSpec, PolygonRegionSpec>;

/// A value held by pointer, so that a spec can hold the variant it belongs to; it is copied by
/// value.
template <typename T> class Boxed {
  public:
    /// A default-constructed value.
    Boxed() : value_(std::make_unique<T>()) {}
    /// A value made from anything `T` can be made from, such as one alternative of a variant.
    /// `std::conjunction` stops at a `Boxed` argument, so copying never asks whether an incomplete
    /// `T` is constructible.
    template <
        typename U,
        std::enable_if_t<
            std::conjunction_v<
                std::negation<std::is_same<std::remove_cv_t<std::remove_reference_t<U>>, Boxed>>,
                std::is_constructible<T, U&&>>,
            int> = 0>
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

  private:
    std::unique_ptr<T> value_;
};

struct MaskedLayerSpec;

/// A layer, named in JSON by its "type".
using LayerSpec =
    std::variant<NoiseLayerSpec, CraterLayerSpec, BoulderLayerSpec, RoughnessLayerSpec,
                 BedformLayerSpec, ScaleLayerSpec, ClampLayerSpec, MaskedLayerSpec,
                 SecondaryLayerSpec, DuneLayerSpec, SandFillLayerSpec, PavementLayerSpec,
                 StrataLayerSpec, SandShadowLayerSpec, SpectralContinuationLayerSpec>;

/// Layer "masked": `layer` applied only inside `shape` (`MaskedLayer`). Boulder ejecta do
/// not see crater layers inside masked layers.
struct MaskedLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;  ///< Meters outside the shape over which the layer fades out.
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};
};

/// A box in surface coordinates, given by its `center` and `size` or by its `min` and `max`
/// corners, each an [x, y] pair; a pair left empty is unset. Surface coordinates are meters on a
/// plane, and longitude and latitude in degrees on a body.
struct ExtentSpec {
    std::vector<double> center{};  ///< [x, y] of the box's center.
    std::vector<double> size{};    ///< [width, height], both positive.
    std::vector<double> min{};     ///< [x, y] of the lower corner.
    std::vector<double> max{};     ///< [x, y] of the upper corner, above `min` along both axes.

    bool operator==(const ExtentSpec& other) const {
        return center == other.center && size == other.size && min == other.min && max == other.max;
    }
    bool operator!=(const ExtentSpec& other) const {
        return !(*this == other);
    }
};

/// The area an output covers and its resolution, snapped to a grid by `config::OutputGrid`.
struct OutputAreaSpec {
    /// Cell size in meters; on a body, meters along a meridian, the same in degrees of longitude.
    std::optional<double> pixelSize{};
    std::optional<ExtentSpec> extentM{};    ///< The extent on a plane, in meters.
    std::optional<ExtentSpec> extentDeg{};  ///< The extent on a body, in degrees.
};

/// A list file to write; the format follows the extension (.json or else CSV) unless given.
///
/// The list covers `area`, or the source's grid without an extent, or else the whole body on a
/// body; features are listed as a heightmap of `area.pixelSize` applies them.
struct ListOutput {
    std::string path;  ///< The file to write; empty to only return the list.
    ListFormat format = ListFormat::Csv;
    OutputAreaSpec area{};
};

/// A float32 GeoTIFF file to write over `area`, with the source's projection.
struct GeoTiffOutput {
    std::string path;  ///< The file to write; empty to only return the rasters.
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;                        ///< Pixels per tile side.
    OutputAreaSpec area{};
};

/// Output "crater_list": writes the generated and manual craters.
struct CraterListOutput : ListOutput {};

/// Output "heightmap": writes the generated heights.
struct HeightmapOutput : GeoTiffOutput {};

/// Output "boulder_list": writes every boulder the layers placed, models and relief alike.
struct BoulderListOutput : ListOutput {};

/// Output "boulder_models": writes the boulders placed as 3D models
/// (`BoulderModelThreshold`) on the ground beneath them, as `WriteBoulderModels` does, over the
/// area a list would cover.
struct BoulderModelsOutput {
    std::string path;           ///< The file to write; empty to only return the models.
    double creaseAngle = 60.0;  ///< Degrees between faces above which vertices are split.
    OutputAreaSpec area{};      ///< As `ListOutput::area`.
    bool instanced = true;      ///< Shared shapes written once and drawn per boulder.
};

/// Output "albedo": writes the albedo (`TerrainGenConfig::albedo`) with one band, or three
/// (red, green, blue).
struct AlbedoOutput : GeoTiffOutput {};

/// Output "mesh": the whole body, as a mesh (`BodyMesh`) with smooth normals in any format
/// `WriteMesh` writes, or as an ICQ shape model (`BodyIcq`) for an .icq path.
struct MeshOutput {
    std::string path;                 ///< The file to write; empty to only return the mesh.
    std::size_t cellsPerFace = 128;   ///< Quads along each edge of the six cube faces.
    std::optional<double> spacing{};  ///< Sample spacing in meters; unset for a quad's side.
};

/// Output "terrain_mesh": the heights over `area` as a mesh (`BuildMesh`) with smooth normals, in
/// any format `WriteMesh` writes and in the frame boulder models are written in.
struct TerrainMeshOutput {
    std::string path;  ///< The file to write; empty to only return the mesh.
    OutputAreaSpec area{};
    /// Boxes in surface coordinates whose quads are left out, such as where a finer mesh covers
    /// the ground; a quad is left out when all four corners lie in one.
    std::vector<ExtentSpec> cutouts{};
    /// Depth in meters of the skirt hung below every boundary edge, inner ones included, to hide
    /// gaps between meshes; 0 for none.
    double skirt = 0.0;
    /// [longitude, latitude] in degrees of the frame's site on a body (`BuildMesh`); empty for the
    /// grid's center.
    std::vector<double> siteDeg{};
};

/// Output "materials": the share of each material class (`TerrainGenConfig::materials`), one band
/// per `MaterialClass` in its order.
struct MaterialsOutput : GeoTiffOutput {};

/// Output "photometry": the Hapke albedo, b, c and roughness the materials mix to, in four bands.
struct PhotometryOutput : GeoTiffOutput {};

/// Output "report": a report of what the configuration generates (`GeneratedOutputs::report`),
/// written as JSON (`ReportToJson`).
struct ReportOutput {
    std::string path;  ///< The file to write; empty to only return the report.
};

/// An output, named in JSON by its "type".
using OutputSpec = std::variant<HeightmapOutput, CraterListOutput, BoulderListOutput,
                                BoulderModelsOutput, AlbedoOutput, MeshOutput, TerrainMeshOutput,
                                MaterialsOutput, PhotometryOutput, ReportOutput>;

/// Albedo source "synthetic": `SyntheticAlbedo`, a base color with a noise stack per
/// channel (each 0 to `kMaxNoiseLayers` layers), measured from the source's reference
/// point.
struct SyntheticAlbedoSpec {
    bool rgb = false;
    double hueDegrees = 0.0;
    double saturation = 0.0;
    double value = kDefaultSyntheticAlbedoValue;
    std::vector<AlbedoNoiseLayerSpec> hueNoiseDegrees;
    std::vector<AlbedoNoiseLayerSpec> saturationNoise;
    std::vector<AlbedoNoiseLayerSpec> valueNoise;

    /// The albedo options, with each noise layer's Hurst exponent applied.
    SyntheticAlbedoOptions Options() const;
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
};

/// An albedo source, named in JSON by its "type".
using AlbedoSourceSpec = std::variant<SyntheticAlbedoSpec, RasterAlbedoSpec>;

/// Albedo layer "craters": `CraterAlbedo`, bright ejecta and rays of the craters of one
/// top-level crater layer, or of every one.
struct CraterAlbedoSpec {
    /// Index of the top-level crater layer, from 0; unset for every crater layer.
    std::optional<std::size_t> craterLayer;
    CraterAlbedoOptions options{};
};

/// Albedo layer "splotches": `SplotchAlbedo`, the marks of secondary impacts over the terrain.
struct SplotchAlbedoSpec {
    SplotchAlbedoOptions options{};
};

/// Albedo layer "blast_zones": `BlastZoneAlbedo`, the zones landers' engines scoured.
struct BlastZoneAlbedoSpec {
    std::vector<BlastZone> zones;
};

/// Albedo layer "rocks": `RockAlbedo`, the rock of the terrain's boulder layers, each read on the
/// ground beneath it.
struct RockAlbedoSpec {
    /// Index of the top-level boulder layer, from 0; unset for every top-level boulder layer.
    std::optional<std::size_t> boulderLayer;
    RockAlbedoOptions options{};
};

/// Albedo layer "slopes": `SlopeAlbedo`, steep ground kept fresh by mass wasting.
struct SlopeAlbedoSpec {
    SlopeAlbedoOptions options{};
};

/// An albedo layer, named in JSON by its "type".
using AlbedoLayerSpec = std::variant<CraterAlbedoSpec, SplotchAlbedoSpec, BlastZoneAlbedoSpec,
                                     RockAlbedoSpec, SlopeAlbedoSpec>;

/// The albedo of the terrain: a source and the layers that change it, evaluated over the
/// terrain's grid.
struct AlbedoSpec {
    AlbedoSourceSpec source{};
    std::vector<AlbedoLayerSpec> layers;
};

/// Body "plane": a projected frame in meters, the default.
struct PlaneSurfaceSpec {};

/// Body "sphere": a body such as a planet or moon (`Surface`); grids and positions are
/// longitude and latitude in degrees, and features are generated over the whole body.
struct SphereSurfaceSpec {
    double radius = 0.0;  ///< Meters; must be given.
};

/// Body "ellipsoid": a body shaped as a triaxial ellipsoid (`Surface::Ellipsoid`), with
/// planetocentric longitude and latitude in degrees and heights along the direction from the
/// center.
struct EllipsoidSurfaceSpec {
    std::vector<double> radii;  ///< Semi-axes a, b, c in meters along x, y and z; exactly three.
};

/// Body "shape_model": a star-shaped body from a shape model file (`Surface::ShapeModel`), an
/// ICQ model or a closed mesh around its origin in body-fixed coordinates, as a `RadialShape`.
struct ShapeModelSurfaceSpec {
    std::string path;
    /// Intervals per face edge of the radial grid; unset keeps an ICQ model's own Q, and uses 256
    /// for a mesh.
    std::optional<std::size_t> q{};
    /// Meters per file unit; unset is 1000 for an ICQ model (kilometers) and 1 for a mesh.
    std::optional<double> metersPerUnit{};
};

/// The body a terrain lies on, a plane or a whole body, named in JSON by its "type".
using SurfaceSpec =
    std::variant<PlaneSurfaceSpec, SphereSurfaceSpec, EllipsoidSurfaceSpec, ShapeModelSurfaceSpec>;

/// A published set of Hapke parameters per material class.
enum class PhotometryReference {
    None,        ///< No parameters; each class present must be given.
    MarsPancam,  ///< `MarsPancamPhotometry`: Pancam fits at 753 nm.
};

/// The Hapke parameters of each material class: `reference`'s, replaced by any given.
struct PhotometrySpec {
    PhotometryReference reference = PhotometryReference::None;
    MaterialPhotometry classes{};  ///< Parameters given per class, over the reference's.
};

/// The material map (`MaterialsAt`), read by "materials" and "photometry" outputs.
struct MaterialsSpec {
    MaterialOptions options{};
    PhotometrySpec photometry{};
};

/// Everything needed to generate a terrain: the body it lies on, optionally its source of heights,
/// the layers added to them in order, the outputs to write, and optionally its albedo and
/// materials.
struct TerrainGenConfig {
    int schemaVersion = kConfigSchemaVersion;
    SurfaceSpec body{};
    /// None for the default synthetic source, `DefaultBaseNoise` at the body's datum.
    std::optional<SourceSpec> source{};
    std::vector<LayerSpec> layers;
    std::vector<OutputSpec> outputs;
    std::optional<AlbedoSpec> albedo;        ///< None by default.
    std::optional<MaterialsSpec> materials;  ///< None by default.
    /// The folder the configuration was read from (`LoadConfig`), which its relative paths
    /// resolve against when a call gives no folder; not part of its JSON.
    std::filesystem::path directory{};
};

/// The folder relative paths of `config` resolve against: `given`, or without one the folder
/// `config` was read from.
inline std::filesystem::path ConfigDirectory(const TerrainGenConfig& config,
                                             const std::filesystem::path& given) {
    return given.empty() ? config.directory : given;
}

/// Whether two configurations are the same, as their JSON is (`ConfigToJson`).
bool operator==(const TerrainGenConfig& a, const TerrainGenConfig& b);

/// Whether two configurations differ, as their JSON does (`ConfigToJson`).
bool operator!=(const TerrainGenConfig& a, const TerrainGenConfig& b);

}  // namespace terraingen
