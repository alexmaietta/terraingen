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
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/region.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/sand_shadow.h"
#include "terraingen/layers/strata.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/pipeline/materials.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/synthetic_terrain.h"

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

/// Smallest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMinConfigGridSize = 2;

/// Largest grid side, in cells, a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// Largest number of cells along a cube face edge of a body mesh or a shape model's radial grid.
inline constexpr std::size_t kMaxCubeFaceCells = 4096;

/// Largest crease angle of boulder models, in degrees.
inline constexpr double kMaxCreaseAngle = 180.0;

/// What GeoTIFF output tile sides, in pixels, must be a multiple of.
inline constexpr std::uint32_t kTileSizeMultiple = 16;

/// Amplitude over wavelength of a terrain noise layer whose amplitude is unset. With it the
/// default 1 km layer has a median slope of 6.4 deg at a 16 m baseline, near the lunar highland
/// median of 7.5 deg at 17 m (Rosenburg et al. 2011, Table 1,
/// https://doi.org/10.1029/2010JE003716), whatever wavelength is chosen.
inline constexpr float kNoiseAmplitudeRatio = 0.1f;

/// Crater shaping by default: irregular outlines on, their elongated share of 5.4% the lunar one
/// of Bottke et al. (2000, https://doi.org/10.1006/icar.1999.6323).
CraterApplyOptions DefaultCraterApply();

/// Boulders by default: up to the largest block of the nominal NASA SP-8023 intercrater curve,
/// about 5.9 m, within the largest rocks NAC images resolve at lunar landing sites, 3 to 19 m
/// (Li et al. 2017, Table 2, https://doi.org/10.1016/j.pss.2017.08.008).
BoulderLayerOptions DefaultBoulders();

/// Roughness by default: octaves up to 16 m, the relief below the 17 m shortest baseline of the
/// LOLA slope statistics the noise layer is matched to (Rosenburg et al. 2011,
/// https://doi.org/10.1029/2010JE003716).
RoughnessOptions DefaultRoughness();

/// Bedforms by default: large wind ripples 4 m apart and 0.4 m high, within the 1 to 5 m
/// wavelengths of large martian ripples that mantle martian dunes ubiquitously (Lapotre et al.
/// 2016, https://doi.org/10.1126/science.aaf3206), so the cover is the layer's most, 0.9; the
/// height keeps the 0.1 height-to-wavelength ratio of `MarsDuneRipples`.
BedformOptions DefaultBedforms();

/// Dunes by default: barchans 50 m wide, about 30 to a square kilometre, as in the barchan field
/// of La Pampa de la Joya, with domes below its 15 m elementary dune size (Elbelrhiti et al. 2008,
/// https://arxiv.org/abs/cond-mat/0609120); a lattice of 129 m cells half of which hold a dune.
DuneOptions DefaultDunes();

/// One noise layer, its octave gain optionally derived from a Hurst exponent.
struct NoiseLayerConfig {
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

    bool operator==(const NoiseLayerConfig& other) const {
        return layer == other.layer && hurst == other.hurst && amplitude == other.amplitude;
    }
    bool operator!=(const NoiseLayerConfig& other) const {
        return !(*this == other);
    }
};

/// Base "raster": a heightmap read from a GeoTIFF, in its own projected coordinates
/// (`GeoTiffSource`).
///
/// Relative paths resolve against the configuration's folder. The raster can be resampled to
/// `pixelSize` metres over the same extent, as when upscaling a DEM; its coordinates must then be
/// a projection in metres.
struct RasterSourceSpec {
    std::string path;
    GeoTiffSourceOptions read{};  ///< How the band is read; `pixelSize` and `lowPass` in metres.

    bool operator==(const RasterSourceSpec& other) const {
        return path == other.path && read == other.read;
    }
    bool operator!=(const RasterSourceSpec& other) const {
        return !(*this == other);
    }
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

    bool operator==(const FieldSpec& other) const {
        return path == other.path && value == other.value && band == other.band &&
               method == other.method && scale == other.scale && fallback == other.fallback &&
               periodicLongitude == other.periodicLongitude;
    }
    bool operator!=(const FieldSpec& other) const {
        return !(*this == other);
    }
};

/// A density map (`DensityMap`) of a crater or boulder population.
struct DensityMapSpec {
    FieldSpec field{};
    DensityMeasure measure = DensityMeasure::Scale;
    double minDiameter = 0.0;             ///< Metres.
    std::optional<double> maxDiameter{};  ///< Metres; unset for no upper limit.
    double areaUnit = 1.0;                ///< Square metres per map area unit.
    bool total = true;

    bool operator==(const DensityMapSpec& other) const {
        return field == other.field && measure == other.measure &&
               minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               areaUnit == other.areaUnit && total == other.total;
    }
    bool operator!=(const DensityMapSpec& other) const {
        return !(*this == other);
    }
};

/// A size slope map (`SizeSlopeMap`) of a boulder population.
struct SizeSlopeMapSpec {
    FieldSpec field{};
    SizeSlopeMeasure measure = SizeSlopeMeasure::Exponent;
    double referenceDiameter = 1.0;  ///< Metres.
    double minExponent = 1.0;
    double maxExponent = 6.0;

    bool operator==(const SizeSlopeMapSpec& other) const {
        return field == other.field && measure == other.measure &&
               referenceDiameter == other.referenceDiameter && minExponent == other.minExponent &&
               maxExponent == other.maxExponent;
    }
    bool operator!=(const SizeSlopeMapSpec& other) const {
        return !(*this == other);
    }
};

struct ChainSourceSpec;

/// The starting heights of a terrain, its base, named in JSON by its "type".
using SourceSpec = std::variant<RasterSourceSpec, ChainSourceSpec>;

/// Base "chain": `sources` tried in order, each filling where those before it have no
/// data (`SourceChain`); its grid is the first source's.
struct ChainSourceSpec {
    std::vector<SourceSpec> sources;

    bool operator==(const ChainSourceSpec& other) const {
        return sources == other.sources;
    }
    bool operator!=(const ChainSourceSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "noise": a stack of 1 to `kMaxNoiseLayers` noise layers added to the terrain
/// (`NoiseRelief`), measured from the base's reference point.
struct NoiseLayerSpec {
    std::vector<NoiseLayerConfig> stack{NoiseLayerConfig{}};

    /// The stack as terrain parameters, with each layer's Hurst exponent applied.
    SyntheticTerrainParams Params() const;

    bool operator==(const NoiseLayerSpec& other) const {
        return stack == other.stack;
    }
    bool operator!=(const NoiseLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Density "power_law": `PowerLawSfd`, N per m^2 with D in metres.
struct PowerLawSpec {
    double coefficient = 0.1;
    double exponent = 2.0;
    bool operator==(const PowerLawSpec& other) const {
        return coefficient == other.coefficient && exponent == other.exponent;
    }
    bool operator!=(const PowerLawSpec& other) const {
        return !(*this == other);
    }
};
/// Density "piecewise_power_law": `PiecewisePowerLawSfd`.
struct PiecewisePowerLawSpec {
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
    bool operator==(const PiecewisePowerLawSpec& other) const {
        return segments == other.segments;
    }
    bool operator!=(const PiecewisePowerLawSpec& other) const {
        return !(*this == other);
    }
};
/// Density "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
struct TabulatedSpec {
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Metres per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square metres per table area unit; 1e6 for km^2.
    bool operator==(const TabulatedSpec& other) const {
        return diameters == other.diameters && cumulative == other.cumulative &&
               diameterUnit == other.diameterUnit && areaUnit == other.areaUnit;
    }
    bool operator!=(const TabulatedSpec& other) const {
        return !(*this == other);
    }
};
/// Density "piecewise_uniform": `PiecewiseUniformSfd`, bands with diameters spread uniformly.
struct PiecewiseUniformSpec {
    std::vector<UniformBand> bands{UniformBand{}};
    double total = 0.0;  ///< Per m^2 across all bands, shared by `density`; 0 for as given.
    bool operator==(const PiecewiseUniformSpec& other) const {
        return bands == other.bands && total == other.total;
    }
    bool operator!=(const PiecewiseUniformSpec& other) const {
        return !(*this == other);
    }
};
/// Crater density "trask": `TraskEquilibrium`.
struct TraskSpec {
    bool operator==(const TraskSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const TraskSpec& other) const {
        return !(*this == other);
    }
};
/// Crater density "hartmann": `HartmannEquilibrium`.
struct HartmannSpec {
    bool operator==(const HartmannSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const HartmannSpec& other) const {
        return !(*this == other);
    }
};
/// Crater density "moore": `MooreDistribution(terrain)`.
struct MooreSpec {
    MooreTerrain terrain = MooreTerrain::RoughMare;
    bool operator==(const MooreSpec& other) const {
        return terrain == other.terrain;
    }
    bool operator!=(const MooreSpec& other) const {
        return !(*this == other);
    }
};
/// Crater density "viper": `ViperSpecification`.
struct ViperSpec {
    bool operator==(const ViperSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const ViperSpec& other) const {
        return !(*this == other);
    }
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
    bool operator==(const ProductionSurfaceSpec& other) const {
        return production == other.production && chronology == other.chronology &&
               ageGyr == other.ageGyr;
    }
    bool operator!=(const ProductionSurfaceSpec& other) const {
        return !(*this == other);
    }
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
    bool operator==(const MinimumSpec& other) const {
        return distributions == other.distributions;
    }
    bool operator!=(const MinimumSpec& other) const {
        return !(*this == other);
    }
};

/// Crater ages "fresh": `FreshAges`.
struct FreshAgesSpec {
    bool operator==(const FreshAgesSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const FreshAgesSpec& other) const {
        return !(*this == other);
    }
};
/// Crater ages "uniform": `UniformAges(lifetimeGyr)`.
struct UniformAgesSpec {
    double lifetimeGyr = 1.0;
    bool operator==(const UniformAgesSpec& other) const {
        return lifetimeGyr == other.lifetimeGyr;
    }
    bool operator!=(const UniformAgesSpec& other) const {
        return !(*this == other);
    }
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
    bool operator==(const SteadyStateAgesSpec& other) const {
        return ageGyr == other.ageGyr && production == other.production &&
               equilibrium == other.equilibrium && chronology == other.chronology;
    }
    bool operator!=(const SteadyStateAgesSpec& other) const {
        return !(*this == other);
    }
};
/// Crater ages "freshness": `FreshnessAges(freshness, lifetimeGyr)`.
struct FreshnessAgesSpec {
    double lifetimeGyr = 1.0;
    ValueDistribution freshness{ValueDistribution::Kind::Uniform, 0.0, 1.0, 0.0, 1.0};
    bool operator==(const FreshnessAgesSpec& other) const {
        return lifetimeGyr == other.lifetimeGyr && freshness == other.freshness;
    }
    bool operator!=(const FreshnessAgesSpec& other) const {
        return !(*this == other);
    }
};
/// A crater age model, named in JSON by its "type".
using AgesSpec =
    std::variant<UniformAgesSpec, FreshAgesSpec, SteadyStateAgesSpec, FreshnessAgesSpec>;

/// Degradation "none": `NoDegradation`, optionally with a fixed depth-to-diameter ratio.
struct NoDegradationSpec {
    std::optional<double> depthRatio;
    bool operator==(const NoDegradationSpec& other) const {
        return depthRatio == other.depthRatio;
    }
    bool operator!=(const NoDegradationSpec& other) const {
        return !(*this == other);
    }
};
/// Degradation "freshness": `FreshnessDegradation`.
struct FreshnessDegradationSpec {
    double cutoffDepthRatio = 0.04;
    bool operator==(const FreshnessDegradationSpec& other) const {
        return cutoffDepthRatio == other.cutoffDepthRatio;
    }
    bool operator!=(const FreshnessDegradationSpec& other) const {
        return !(*this == other);
    }
};
/// Where "diffusion" degradation takes its diffusion ages from.
enum class DiffusivityModel {
    /// "matched": with steady-state ages, `EquilibriumDiffusionAges`, which wears each crater of
    /// the layer's profile to Fassett et al.'s (2022) visibility cutoff, d/D 0.04, at the end of
    /// its equilibrium lifetime, as they derive the diffusivity; `Fassett2022Hartmann` for other
    /// ages.
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
    bool operator==(const DiffusionDegradationSpec& other) const {
        return diffusivity == other.diffusivity && powerLaw == other.powerLaw &&
               reachRadii == other.reachRadii;
    }
    bool operator!=(const DiffusionDegradationSpec& other) const {
        return !(*this == other);
    }
};
/// A degradation model, named in JSON by its "type".
using DegradationSpec =
    std::variant<FreshnessDegradationSpec, NoDegradationSpec, DiffusionDegradationSpec>;

/// Crater profile "fassett_thomson": `FassettThomsonProfile(rim)`.
struct FassettThomsonSpec {
    FassettThomsonRim rim = FassettThomsonRim::Original;
    bool operator==(const FassettThomsonSpec& other) const {
        return rim == other.rim;
    }
    bool operator!=(const FassettThomsonSpec& other) const {
        return !(*this == other);
    }
};
/// Crater profile "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
struct MartinParkesDunstanSpec {
    bool operator==(const MartinParkesDunstanSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const MartinParkesDunstanSpec& other) const {
        return !(*this == other);
    }
};
/// A simple crater profile, named in JSON by its "type".
using SimpleProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;
/// Crater profile "complex": `simple` below the simple-to-complex transition of the layer's
/// morphometry, and complex craters, with flat floors and central peaks, from it
/// (`ComplexCraterProfile` with `LunarComplexCraterShape` or `MarsComplexCraterShape`).
struct ComplexProfileSpec {
    SimpleProfileSpec simple{MartinParkesDunstanSpec{}};
    bool operator==(const ComplexProfileSpec& other) const {
        return simple == other.simple;
    }
    bool operator!=(const ComplexProfileSpec& other) const {
        return !(*this == other);
    }
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
    /// By default a 3.5 Gyr extended Neukum production surface up to Hartmann (1984) equilibrium,
    /// as the DSNE (SLS-SPEC-159 Rev. I, 3.4.1.1) builds its 3.5 Ga mare density but with the lower
    /// of the two equilibria that small-crater counts fall between (Fassett 2022, eq. 3 and
    /// section 3, https://doi.org/10.1029/2022JE007510): below Trask, the DSNE's upper limit, at
    /// every diameter under about 1.7 km.
    DensitySpec density{MinimumSpec{{ProductionSurfaceSpec{}, HartmannSpec{}}}};
    /// By default steady-state ages on that surface, matching the density.
    AgesSpec ages{SteadyStateAgesSpec{}};
    /// Metres; by default 2.5, where the extended Neukum production function the default density
    /// and ages use begins.
    double minDiameter = 2.5;
    double maxDiameter = 250.0;  ///< Metres.
    CraterSpacing spacing{};
    std::size_t maxCraters = 0;  ///< Keep at most this many generated craters; 0 keeps all.
    /// Manual craters, in the surface's coordinates; parsing sets their `manual` flag.
    std::vector<Crater> manual{};
    ProfileSpec profile{MartinParkesDunstanSpec{}};
    /// By default diffusion matched to those ages: about a quarter of the craters keep 3/4 of
    /// their fresh depth, as in Moore's steady state, and none wear past the visibility cutoff.
    DegradationSpec degradation{DiffusionDegradationSpec{}};
    CraterApplyOptions apply{DefaultCraterApply()};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `CraterLayerOptions::densityMap`.
    /// Gives `generation.completeness`: where `manual` is a complete catalog, from which diameter.
    std::optional<FieldSpec> completeness{};

    bool operator==(const CraterLayerSpec& other) const {
        return morphometry == other.morphometry && generate == other.generate &&
               seed == other.seed && density == other.density && ages == other.ages &&
               minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               spacing == other.spacing && maxCraters == other.maxCraters &&
               manual == other.manual && profile == other.profile &&
               degradation == other.degradation && apply == other.apply &&
               densityMap == other.densityMap && completeness == other.completeness;
    }
    bool operator!=(const CraterLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Boulder sizes "golombek_rapp": the layer's coverage, qa and qb.
struct GolombekRappBouldersSpec {
    bool operator==(const GolombekRappBouldersSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const GolombekRappBouldersSpec& other) const {
        return !(*this == other);
    }
};
/// Boulder sizes "sp8023_intercrater": `Sp8023IntercraterBlocks(curve)`.
struct Sp8023IntercraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023IntercraterBouldersSpec& other) const {
        return curve == other.curve;
    }
    bool operator!=(const Sp8023IntercraterBouldersSpec& other) const {
        return !(*this == other);
    }
};
/// Boulder sizes "sp8023_crater": `Sp8023CraterBlocks(curve)`.
struct Sp8023CraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
    bool operator==(const Sp8023CraterBouldersSpec& other) const {
        return curve == other.curve;
    }
    bool operator!=(const Sp8023CraterBouldersSpec& other) const {
        return !(*this == other);
    }
};
/// Boulder sizes "synthterrain": `SynthterrainBoulderDistribution(model)`.
struct SynthterrainBouldersSpec {
    SynthterrainBoulders model = SynthterrainBoulders::InterCrater;
    bool operator==(const SynthterrainBouldersSpec& other) const {
        return model == other.model;
    }
    bool operator!=(const SynthterrainBouldersSpec& other) const {
        return !(*this == other);
    }
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
    BoulderLayerOptions boulders{DefaultBoulders()};
    /// Gives `boulders.sizes.distribution`. By default the nominal NASA SP-8023 intercrater blocks
    /// (Figure 7), whose rocks of 1 m and more cover 0.4% of the ground: between the lunar mean of
    /// 0.3% and the maria mean of 0.5% from Diviner (SLS-SPEC-159 Rev. I, 3.4.1.4). The DSNE's
    /// Surveyor 1 design case, 0.234 D^-2.11 per 100 m^2, is a 91st-percentile site (0.8%).
    BoulderSizesSpec sizes{Sp8023IntercraterBouldersSpec{SpecificationCurve::Nominal}};
    /// Gives `boulders.ejecta.distribution`; unset follows `sizes`.
    std::optional<BoulderSizesSpec> ejectaSizes{};
    bool extrapolate = false;  ///< Let tables be evaluated beyond their published diameters.
    /// Rock meshes (.obj, .gltf or .glb) that replace the procedural shapes, as
    /// `BoulderShapeOptions::library`; relative to the configuration's folder.
    std::vector<std::string> shapeFiles{};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `boulders.densityMap`.
    std::optional<SizeSlopeMapSpec> slopeMap{};  ///< Gives `boulders.slopeMap`.
    /// Catalog boulders in the surface's coordinates, as `boulders.manual`.
    std::vector<Crater> manual{};
    std::optional<FieldSpec> completeness{};  ///< Gives `boulders.completeness`.

    bool operator==(const BoulderLayerSpec& other) const {
        return boulders == other.boulders && sizes == other.sizes &&
               ejectaSizes == other.ejectaSizes && extrapolate == other.extrapolate &&
               shapeFiles == other.shapeFiles && densityMap == other.densityMap &&
               slopeMap == other.slopeMap && manual == other.manual &&
               completeness == other.completeness;
    }
    bool operator!=(const BoulderLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "roughness": fine roughness of a given RMS slope (`RoughnessLayer`).
struct RoughnessLayerSpec {
    RoughnessOptions roughness{DefaultRoughness()};

    bool operator==(const RoughnessLayerSpec& other) const {
        return roughness == other.roughness;
    }
    bool operator!=(const RoughnessLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "bedforms": fields of wind-blown ripples or dunes (`BedformLayer`); angles in
/// radians.
struct BedformLayerSpec {
    BedformOptions bedforms{DefaultBedforms()};

    bool operator==(const BedformLayerSpec& other) const {
        return bedforms == other.bedforms;
    }
    bool operator!=(const BedformLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "dunes": barchans, domes or crest-line dunes with ripples on them (`DuneLayer`); angles
/// in radians.
struct DuneLayerSpec {
    DuneOptions dunes{DefaultDunes()};

    bool operator==(const DuneLayerSpec& other) const {
        return dunes == other.dunes;
    }
    bool operator!=(const DuneLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "pavement": fractured bedrock plates parted by cracks or ridges (`PavementLayer`);
/// angles in radians.
struct PavementLayerSpec {
    PavementOptions pavement{};

    bool operator==(const PavementLayerSpec& other) const {
        return pavement == other.pavement;
    }
    bool operator!=(const PavementLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "strata": the ground beneath terraced into beds of benches and cliffs (`StrataLayer`);
/// angles in radians.
struct StrataLayerSpec {
    StrataOptions strata{};

    bool operator==(const StrataLayerSpec& other) const {
        return strata == other.strata;
    }
    bool operator!=(const StrataLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "sand_shadow": sand tails in the lee of the boulders of the nearest boulder layer before
/// it (`SandShadowLayer`); angles in radians.
struct SandShadowLayerSpec {
    SandShadowOptions shadows{};

    bool operator==(const SandShadowLayerSpec& other) const {
        return shadows == other.shadows;
    }
    bool operator!=(const SandShadowLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "sand_fill": sand pooled in the lows of the ground beneath it (`SandFillLayer`).
struct SandFillLayerSpec {
    SandFillOptions sandFill{};

    bool operator==(const SandFillLayerSpec& other) const {
        return sandFill == other.sandFill;
    }
    bool operator!=(const SandFillLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "secondaries": secondary craters around the craters of the last crater layer
/// before this one (`SecondaryCraterLayer`), shaped with that layer's profile at a fixed depth
/// ratio and the fresh rim of `morphometry`. `secondaries.onsetCount` must be given.
struct SecondaryLayerSpec {
    SecondaryCraterOptions secondaries{};
    /// Depth-to-diameter ratio.
    double depthRatio = 0.11;
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;

    bool operator==(const SecondaryLayerSpec& other) const {
        return secondaries == other.secondaries && depthRatio == other.depthRatio &&
               morphometry == other.morphometry;
    }
    bool operator!=(const SecondaryLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "dem_detail": the detail a GeoTIFF DEM has above its own low-pass, added to the heights
/// (`DetailLayer`), to restore measured relief over a source that keeps only the DEM's low
/// frequencies.
struct DemDetailLayerSpec {
    std::string path;
    /// How the DEM is read; its `lowPass` is required.
    GeoTiffSourceOptions read = [] {
        GeoTiffSourceOptions options;
        options.lowPass = 50.0;
        return options;
    }();
    double weight = 1.0;  ///< Share of the detail added; 1 restores it whole.

    bool operator==(const DemDetailLayerSpec& other) const {
        return path == other.path && read == other.read && weight == other.weight;
    }
    bool operator!=(const DemDetailLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "scale": height * `scale` + `offset` (`ScaleLayer`).
struct ScaleLayerSpec {
    double scale = 1.0;
    double offset = 0.0;  ///< Metres.

    bool operator==(const ScaleLayerSpec& other) const {
        return scale == other.scale && offset == other.offset;
    }
    bool operator!=(const ScaleLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Layer "clamp": heights clamped to [`min`, `max`], either optional (`ClampLayer`).
struct ClampLayerSpec {
    std::optional<double> min;  ///< Metres; unset for no lower bound.
    std::optional<double> max;  ///< Metres; unset for no upper bound.

    bool operator==(const ClampLayerSpec& other) const {
        return min == other.min && max == other.max;
    }
    bool operator!=(const ClampLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Region shape "rectangle": [`minX`, `maxX`] x [`minY`, `maxY`] in surface coordinates.
struct RectangleRegionSpec {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool operator==(const RectangleRegionSpec& other) const {
        return minX == other.minX && minY == other.minY && maxX == other.maxX && maxY == other.maxY;
    }
    bool operator!=(const RectangleRegionSpec& other) const {
        return !(*this == other);
    }
};
/// Region shape "circle": centred on (`x`, `y`) in surface coordinates.
struct CircleRegionSpec {
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;  ///< Metres.
    bool operator==(const CircleRegionSpec& other) const {
        return x == other.x && y == other.y && radius == other.radius;
    }
    bool operator!=(const CircleRegionSpec& other) const {
        return !(*this == other);
    }
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
    template <
        typename U,
        std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<U>>, Boxed> &&
                             std::is_constructible_v<T, U&&>,
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

    friend bool operator==(const Boxed& a, const Boxed& b) {
        return *a.value_ == *b.value_;
    }
    friend bool operator!=(const Boxed& a, const Boxed& b) {
        return !(a == b);
    }

  private:
    std::unique_ptr<T> value_;
};

struct RegionLayerSpec;

/// A layer, named in JSON by its "type".
using LayerSpec =
    std::variant<NoiseLayerSpec, CraterLayerSpec, BoulderLayerSpec, RoughnessLayerSpec,
                 BedformLayerSpec, ScaleLayerSpec, ClampLayerSpec, RegionLayerSpec,
                 SecondaryLayerSpec, DemDetailLayerSpec, DuneLayerSpec, SandFillLayerSpec,
                 PavementLayerSpec, StrataLayerSpec, SandShadowLayerSpec>;

/// Layer "region": `layer` applied only inside `shape` (`RegionLayer`). Boulder ejecta do
/// not see crater layers inside regions.
struct RegionLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;  ///< Metres outside the shape over which the layer fades out.
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};

    bool operator==(const RegionLayerSpec& other) const {
        return shape == other.shape && feather == other.feather && blend == other.blend &&
               layer == other.layer;
    }
    bool operator!=(const RegionLayerSpec& other) const {
        return !(*this == other);
    }
};

/// Formats for crater and boulder lists.
enum class ListFormat { Csv, Json };

/// The north-up grid of square cells an output covers: `width` x `height` cells of `pixelSize`
/// centred on (`centreX`, `centreY`).
///
/// On a body the centre is a longitude and latitude, and cells are `pixelSize` metres along a
/// meridian, the same in degrees of longitude.
struct GridRegionSpec {
    std::size_t width = 1025;   ///< Cells.
    std::size_t height = 1025;  ///< Cells.
    double pixelSize = 1.0;     ///< Metres.
    double centreX = 0.0;       ///< Centre in surface coordinates.
    double centreY = 0.0;       ///< Centre in surface coordinates.

    bool operator==(const GridRegionSpec& other) const {
        return width == other.width && height == other.height && pixelSize == other.pixelSize &&
               centreX == other.centreX && centreY == other.centreY;
    }
    bool operator!=(const GridRegionSpec& other) const {
        return !(*this == other);
    }
};

/// A list file to write; the format follows the extension (.json or else CSV) unless given.
///
/// The list covers `region`, or the base's grid without one, or else the whole body on a body.
struct ListOutput {
    std::string path;
    ListFormat format = ListFormat::Csv;
    std::optional<GridRegionSpec> region{};
    /// Sample spacing in metres the features are listed at, as a heightmap of that pixel size
    /// applies them; unset for the grid's. A whole-body list needs it.
    std::optional<double> spacing{};
    bool operator==(const ListOutput& other) const {
        return path == other.path && format == other.format && region == other.region &&
               spacing == other.spacing;
    }
    bool operator!=(const ListOutput& other) const {
        return !(*this == other);
    }
};

/// A float32 GeoTIFF file to write over `region`, or the base's grid without one, with the base's
/// projection.
struct GeoTiffOutput {
    std::string path;
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;                        ///< Pixels per tile side.
    std::optional<GridRegionSpec> region{};
    bool operator==(const GeoTiffOutput& other) const {
        return path == other.path && compression == other.compression &&
               tileSize == other.tileSize && region == other.region;
    }
    bool operator!=(const GeoTiffOutput& other) const {
        return !(*this == other);
    }
};

/// Output "crater_list": writes the generated and manual craters.
struct CraterListOutput : ListOutput {};

/// Output "heightmap": writes the generated heights.
struct HeightmapOutput : GeoTiffOutput {};

/// Output "boulder_list": writes every boulder the layers placed, models and relief alike.
struct BoulderListOutput : ListOutput {};

/// Output "boulder_models": writes the boulders placed as 3D models
/// (`BoulderModelThreshold`) on the ground beneath them, as `WriteBoulderModels` does, over the
/// grid a list would cover.
struct BoulderModelsOutput {
    std::string path;
    double creaseAngle = 60.0;  ///< Degrees between faces above which vertices are split.
    std::optional<GridRegionSpec> region{};
    std::optional<double> spacing{};  ///< As `ListOutput::spacing`.
    bool instanced = true;            ///< Shared shapes written once and drawn per boulder.
    bool operator==(const BoulderModelsOutput& other) const {
        return path == other.path && creaseAngle == other.creaseAngle && region == other.region &&
               spacing == other.spacing && instanced == other.instanced;
    }
    bool operator!=(const BoulderModelsOutput& other) const {
        return !(*this == other);
    }
};

/// Output "albedo": writes the albedo (`TerrainConfig::albedo`) with one band, or three
/// (red, green, blue).
struct AlbedoOutput : GeoTiffOutput {};

/// Output "mesh": the whole body, as a mesh (`BodyMesh`) with smooth normals in any format
/// `WriteMesh` writes, or as an ICQ shape model (`BodyIcq`) for an .icq path.
struct MeshOutput {
    std::string path;
    std::size_t cellsPerFace = 128;   ///< Quads along each edge of the six cube faces.
    std::optional<double> spacing{};  ///< Sample spacing in metres; unset for a quad's side.
    bool operator==(const MeshOutput& other) const {
        return path == other.path && cellsPerFace == other.cellsPerFace && spacing == other.spacing;
    }
    bool operator!=(const MeshOutput& other) const {
        return !(*this == other);
    }
};

/// Output "terrain_mesh": the heights over `region`, or the base's grid without one, as a mesh
/// (`HeightMesh`) with smooth normals, in any format `WriteMesh` writes and in the frame boulder
/// models are written in.
struct TerrainMeshOutput {
    std::string path;
    std::optional<GridRegionSpec> region{};
    bool operator==(const TerrainMeshOutput& other) const {
        return path == other.path && region == other.region;
    }
    bool operator!=(const TerrainMeshOutput& other) const {
        return !(*this == other);
    }
};

/// Output "materials": the share of each material class (`TerrainConfig::materials`), one band
/// per `MaterialClass` in its order.
struct MaterialsOutput : GeoTiffOutput {};

/// Output "photometry": the Hapke albedo, b, c and roughness the materials mix to, in four bands.
struct PhotometryOutput : GeoTiffOutput {};

/// An output, named in JSON by its "type".
using OutputSpec =
    std::variant<HeightmapOutput, CraterListOutput, BoulderListOutput, BoulderModelsOutput,
                 AlbedoOutput, MeshOutput, TerrainMeshOutput, MaterialsOutput, PhotometryOutput>;

/// Albedo source "synthetic": `SyntheticAlbedo`, a base colour with a noise stack per
/// channel (each 0 to `kMaxNoiseLayers` layers), measured from the base's reference
/// point.
struct SyntheticAlbedoSpec {
    bool rgb = false;
    double hue = 0.0;
    double saturation = 0.0;
    double value = kDefaultSyntheticAlbedoValue;
    std::vector<NoiseLayerConfig> hueNoise;
    std::vector<NoiseLayerConfig> saturationNoise;
    std::vector<NoiseLayerConfig> valueNoise;

    /// The albedo parameters, with each noise layer's Hurst exponent applied.
    SyntheticAlbedoOptions Params() const;

    bool operator==(const SyntheticAlbedoSpec& other) const {
        return rgb == other.rgb && hue == other.hue && saturation == other.saturation &&
               value == other.value && hueNoise == other.hueNoise &&
               saturationNoise == other.saturationNoise && valueNoise == other.valueNoise;
    }
    bool operator!=(const SyntheticAlbedoSpec& other) const {
        return !(*this == other);
    }
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

    bool operator==(const RasterAlbedoSpec& other) const {
        return path == other.path && bands == other.bands && scale == other.scale &&
               offset == other.offset && options == other.options;
    }
    bool operator!=(const RasterAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// An albedo source, named in JSON by its "type".
using AlbedoSourceSpec = std::variant<SyntheticAlbedoSpec, RasterAlbedoSpec>;

/// Albedo layer "craters": `CraterAlbedo`, bright ejecta and rays of the craters of one
/// top-level crater layer, or of every one.
struct CraterAlbedoSpec {
    /// Index of the top-level crater layer, from 0; unset for every crater layer.
    std::optional<std::size_t> craterLayer;
    CraterAlbedoOptions options{};

    bool operator==(const CraterAlbedoSpec& other) const {
        return craterLayer == other.craterLayer && options == other.options;
    }
    bool operator!=(const CraterAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// Albedo layer "splotches": `SplotchAlbedo`, the marks of secondary impacts over the terrain.
struct SplotchAlbedoSpec {
    SplotchAlbedoOptions options{};

    bool operator==(const SplotchAlbedoSpec& other) const {
        return options == other.options;
    }
    bool operator!=(const SplotchAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// Albedo layer "blast_zones": `BlastZoneAlbedo`, the zones landers' engines scoured.
struct BlastZoneAlbedoSpec {
    std::vector<BlastZone> zones;

    bool operator==(const BlastZoneAlbedoSpec& other) const {
        return zones == other.zones;
    }
    bool operator!=(const BlastZoneAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// Albedo layer "rocks": `RockAlbedo`, the rock of the terrain's boulder layers, each read on the
/// ground beneath it.
struct RockAlbedoSpec {
    /// Index of the top-level boulder layer, from 0; unset for every top-level boulder layer.
    std::optional<std::size_t> boulderLayer;
    RockAlbedoOptions options{};

    bool operator==(const RockAlbedoSpec& other) const {
        return boulderLayer == other.boulderLayer && options == other.options;
    }
    bool operator!=(const RockAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// Albedo layer "slopes": `SlopeAlbedo`, steep ground kept fresh by mass wasting.
struct SlopeAlbedoSpec {
    SlopeAlbedoOptions options{};

    bool operator==(const SlopeAlbedoSpec& other) const {
        return options == other.options;
    }
    bool operator!=(const SlopeAlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// An albedo layer, named in JSON by its "type".
using AlbedoLayerSpec = std::variant<CraterAlbedoSpec, SplotchAlbedoSpec, BlastZoneAlbedoSpec,
                                     RockAlbedoSpec, SlopeAlbedoSpec>;

/// The albedo of the terrain: a source and the layers that change it, evaluated over the
/// terrain's grid.
struct AlbedoSpec {
    AlbedoSourceSpec source{};
    std::vector<AlbedoLayerSpec> layers;

    bool operator==(const AlbedoSpec& other) const {
        return source == other.source && layers == other.layers;
    }
    bool operator!=(const AlbedoSpec& other) const {
        return !(*this == other);
    }
};

/// Body "plane": a projected frame in metres, the default.
struct PlaneSurfaceSpec {
    bool operator==(const PlaneSurfaceSpec& /*other*/) const {
        return true;
    }
    bool operator!=(const PlaneSurfaceSpec& other) const {
        return !(*this == other);
    }
};

/// Body "sphere": a body such as a planet or moon (`Surface`); grids and positions are
/// longitude and latitude in degrees, and features are generated over the whole body.
struct SphereSurfaceSpec {
    double radius = 0.0;  ///< Metres; must be given.
    bool operator==(const SphereSurfaceSpec& other) const {
        return radius == other.radius;
    }
    bool operator!=(const SphereSurfaceSpec& other) const {
        return !(*this == other);
    }
};

/// Body "ellipsoid": a body shaped as a triaxial ellipsoid (`Surface::Ellipsoid`), with
/// planetocentric longitude and latitude in degrees and heights along the direction from the
/// centre.
struct EllipsoidSurfaceSpec {
    std::vector<double> radii;  ///< Semi-axes a, b, c in metres along x, y and z; exactly three.
    bool operator==(const EllipsoidSurfaceSpec& other) const {
        return radii == other.radii;
    }
    bool operator!=(const EllipsoidSurfaceSpec& other) const {
        return !(*this == other);
    }
};

/// Body "shape_model": a star-shaped body from a shape model file (`Surface::ShapeModel`), an
/// ICQ model or a closed mesh around its origin in body-fixed coordinates, as a `RadialShape`.
struct ShapeModelSurfaceSpec {
    std::string path;
    /// Intervals per face edge of the radial grid; unset keeps an ICQ model's own Q, and uses 256
    /// for a mesh.
    std::optional<std::size_t> q{};
    /// Metres per file unit; unset is 1000 for an ICQ model (kilometres) and 1 for a mesh.
    std::optional<double> metresPerUnit{};
    bool operator==(const ShapeModelSurfaceSpec& other) const {
        return path == other.path && q == other.q && metresPerUnit == other.metresPerUnit;
    }
    bool operator!=(const ShapeModelSurfaceSpec& other) const {
        return !(*this == other);
    }
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

    bool operator==(const PhotometrySpec& other) const {
        return reference == other.reference && classes == other.classes;
    }
    bool operator!=(const PhotometrySpec& other) const {
        return !(*this == other);
    }
};

/// The material map (`MaterialsAt`), read by "materials" and "photometry" outputs.
struct MaterialsSpec {
    MaterialOptions options{};
    PhotometrySpec photometry{};

    bool operator==(const MaterialsSpec& other) const {
        return options == other.options && photometry == other.photometry;
    }
    bool operator!=(const MaterialsSpec& other) const {
        return !(*this == other);
    }
};

/// Everything needed to generate a terrain: the body it lies on, optionally its base heights, the
/// layers added to them in order, the outputs to write, and optionally its albedo and materials.
struct TerrainConfig {
    int schemaVersion = kConfigSchemaVersion;
    SurfaceSpec body{};
    std::optional<SourceSpec> base{};  ///< None for the body's datum, 0 m, everywhere.
    std::vector<LayerSpec> layers;
    std::vector<OutputSpec> outputs;
    std::optional<AlbedoSpec> albedo;        ///< None by default.
    std::optional<MaterialsSpec> materials;  ///< None by default.

    bool operator==(const TerrainConfig& other) const {
        return schemaVersion == other.schemaVersion && body == other.body && base == other.base &&
               layers == other.layers && outputs == other.outputs && albedo == other.albedo &&
               materials == other.materials;
    }
    bool operator!=(const TerrainConfig& other) const {
        return !(*this == other);
    }
};

}  // namespace terraingen
