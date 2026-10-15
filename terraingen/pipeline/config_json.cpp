#include "terraingen/pipeline/config_json.h"

#include "terraingen/pipeline/config_checks.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;
using config::Child;
using config::Element;

std::string TypeName(const Json& json) {
    return json.type_name();
}

[[noreturn]] void Fail(const std::string& path, const std::string& message) {
    throw ConfigError(path, message);
}

/// One named value of an enum in the configuration, and what it means, for editors
/// (`ConfigSchema`).
template <typename E> struct EnumEntry {
    std::string_view name;
    E value;
    std::string_view description{};
};

template <typename E> struct EnumNames;

template <> struct EnumNames<NoiseBasis> {
    static constexpr std::array<EnumEntry<NoiseBasis>, 6> kValues{{
        {"perlin", NoiseBasis::Perlin,
         "Improved Perlin gradient noise: smooth, general-purpose relief."},
        {"simplex", NoiseBasis::Simplex,
         "Simplex gradient noise: smooth relief, an alternative to Perlin."},
        {"value", NoiseBasis::Value,
         "Value noise: random values interpolated over a lattice; blockier than gradient noise."},
        {"worley_f1", NoiseBasis::WorleyF1,
         "Cellular noise from the distance to the nearest feature point: cones and basins."},
        {"worley_f2", NoiseBasis::WorleyF2,
         "Cellular noise from the distance to the second-nearest feature point."},
        {"worley_f2_minus_f1", NoiseBasis::WorleyF2MinusF1,
         "Cellular noise F2 - F1: ridges along cell borders, like cracked ground."},
    }};
};

template <> struct EnumNames<FractalType> {
    static constexpr std::array<EnumEntry<FractalType>, 4> kValues{{
        {"fbm", FractalType::Fbm,
         "Fractional Brownian motion: a weighted sum of octaves, the general-purpose choice."},
        {"ridged", FractalType::Ridged,
         "Ridged multifractal: sharp crests from inverted absolute noise, like eroded ridges."},
        {"billow", FractalType::Billow, "Sum of absolute-value octaves: rounded, puffy forms."},
        {"hybrid_multifractal", FractalType::HybridMultifractal,
         "Hybrid multifractal: smooth lowlands and rough highlands."},
    }};
};

template <> struct EnumNames<LayerBlend> {
    static constexpr std::array<EnumEntry<LayerBlend>, 4> kValues{{
        {"add", LayerBlend::Add, "Adds the layer to the height so far."},
        {"max", LayerBlend::Max, "Keeps the higher of the height so far and the layer."},
        {"min", LayerBlend::Min, "Keeps the lower of the height so far and the layer."},
        {"mask", LayerBlend::Mask,
         "Multiplies the height so far by (layer / amplitude + 1) / 2, so the layer modulates "
         "roughness by region."},
    }};
};

template <> struct EnumNames<MooreTerrain> {
    static constexpr std::array<EnumEntry<MooreTerrain>, 4> kValues{{
        {"rough_mare", MooreTerrain::RoughMare,
         "Moore's rough mare crater distribution: 10^-1 D^-2 per m^2 below 100 m, 10 D^-3 above."},
        {"smooth_mare", MooreTerrain::SmoothMare,
         "Moore's smooth mare crater distribution: 10^-1 D^-2 per m^2 below 40 m, then D^-3, "
         "D^-1.68 from 100 m and D^-3 from 200 m."},
        {"uplands", MooreTerrain::Uplands,
         "Moore's uplands crater distribution: 10^-1 D^-2 per m^2 below 1 km, 10^2 D^-3 above."},
        {"heavily_cratered", MooreTerrain::HeavilyCratered,
         "Moore's heavily cratered distribution: 10^-1 D^-2 per m^2 up to 10 km."},
    }};
};

template <> struct EnumNames<EquilibriumModel> {
    static constexpr std::array<EnumEntry<EquilibriumModel>, 2> kValues{{
        {"trask", EquilibriumModel::Trask,
         "Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2, as the DSNE uses."},
        {"hartmann", EquilibriumModel::Hartmann,
         "Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D^-1.83 per km^2 with D in km."},
    }};
};

template <> struct EnumNames<FassettThomsonRim> {
    static constexpr std::array<EnumEntry<FassettThomsonRim>, 3> kValues{{
        {"original", FassettThomsonRim::Original,
         "The published fits, whose interior and exterior meet at 0.98 radii."},
        {"at_one_radius", FassettThomsonRim::AtOneRadius,
         "Interior fit extended to 1.0 radii so the rim lies at the stated diameter, as "
         "synthterrain's FT_Crater; leaves a small step at the rim."},
        {"plateau", FassettThomsonRim::Plateau,
         "A flat rim at the fresh rim height from 0.98 to 1.02 radii, after Fassett et al. (2022), "
         "as synthterrain's FTmod_Crater."},
    }};
};

template <> struct EnumNames<CraterOrder> {
    static constexpr std::array<EnumEntry<CraterOrder>, 2> kValues{{
        {"oldest_first", CraterOrder::OldestFirst,
         "Oldest craters first, then larger first: the physical order, so younger craters "
         "overprint older ones."},
        {"largest_first", CraterOrder::LargestFirst,
         "Largest craters first, then older first, so smaller craters overprint larger ones."},
    }};
};

template <> struct EnumNames<CraterDatum> {
    static constexpr std::array<EnumEntry<CraterDatum>, 4> kValues{{
        {"none", CraterDatum::None,
         "No datum: the crater profile is added onto the terrain everywhere."},
        {"plane", CraterDatum::Plane,
         "Interior cut relative to a least-squares plane over the fit footprint, keeping regional "
         "slopes."},
        {"mean_height", CraterDatum::MeanHeight,
         "Interior cut relative to a flat datum at the mean height over the fit footprint."},
        {"centre_height", CraterDatum::CentreHeight,
         "Interior cut relative to a flat datum at the terrain height under the crater centre."},
    }};
};

template <> struct EnumNames<BlendShape> {
    static constexpr std::array<EnumEntry<BlendShape>, 4> kValues{{
        {"linear", BlendShape::Linear, "Straight ramp: continuous, with kinks at both ends."},
        {"smoothstep", BlendShape::Smoothstep,
         "Cubic 3t^2 - 2t^3: continuous slope across the transition."},
        {"smootherstep", BlendShape::Smootherstep,
         "Quintic 6t^5 - 15t^4 + 10t^3: continuous slope and curvature, the smoothest join."},
        {"gaussian", BlendShape::Gaussian,
         "Gaussian falloff with sigma a third of the band, rescaled to end at zero."},
    }};
};

template <> struct EnumNames<EjectaProfile> {
    static constexpr std::array<EnumEntry<EjectaProfile>, 2> kValues{{
        {"smoothstep", EjectaProfile::Smoothstep,
         "Every block inside the rim, falling along a smoothstep to none at the reach."},
        {"exponential", EjectaProfile::Exponential,
         "Exponential decay outside the rim, a fixed share on the floor and none beyond the reach: "
         "synthterrain's crater probability map."},
    }};
};

template <> struct EnumNames<EjectaAging> {
    static constexpr std::array<EnumEntry<EjectaAging>, 2> kValues{{
        {"freshness", EjectaAging::Freshness,
         "Blocks wear away with the crater's freshness (current over fresh depth): all when fresh, "
         "none at or below the minimum freshness."},
        {"age", EjectaAging::Age,
         "Blocks wear away with the crater's age as (1 - age / lifetime)^exponent, synthterrain's "
         "form."},
    }};
};

template <> struct EnumNames<SpecificationCurve> {
    static constexpr std::array<EnumEntry<SpecificationCurve>, 3> kValues{{
        {"minimum", SpecificationCurve::Minimum, "The model's minimum curve: the fewest boulders."},
        {"nominal", SpecificationCurve::Nominal, "The model's nominal curve."},
        {"maximum", SpecificationCurve::Maximum, "The model's maximum curve: the most boulders."},
    }};
};

template <> struct EnumNames<SynthterrainBoulders> {
    static constexpr std::array<EnumEntry<SynthterrainBoulders>, 3> kValues{{
        {"intercrater", SynthterrainBoulders::InterCrater,
         "0.0001 D^-1.75457 per m^2 with D in metres: synthetic_moon's \"low blockiness\"."},
        {"haworth", SynthterrainBoulders::Haworth,
         "0.002 D^-2.6607 per m^2 with D in metres: synthetic_moon's \"high blockiness\"."},
        {"viper_specification", SynthterrainBoulders::ViperSpecification,
         "0.0003 D^-2.482 per m^2 with D in metres: the VIPER Environmental Specification."},
    }};
};

template <> struct EnumNames<BaseSolid> {
    static constexpr std::array<EnumEntry<BaseSolid>, 5> kValues{{
        {"icosahedron", BaseSolid::Icosahedron,
         "Boulders start from an icosahedron, twenty triangles on the unit sphere: rounder "
         "shapes."},
        {"cube", BaseSolid::Cube, "Boulders start from a cube: blocky shapes."},
        {"pyramid", BaseSolid::Pyramid, "Boulders start from a square-based pyramid."},
        {"prism", BaseSolid::Prism, "Boulders start from a triangular prism, a wedge."},
        {"tetrahedron", BaseSolid::Tetrahedron,
         "Boulders start from a regular tetrahedron: angular shapes."},
    }};
};

template <> struct EnumNames<ValueDistribution::Kind> {
    static constexpr std::array<EnumEntry<ValueDistribution::Kind>, 4> kValues{{
        {"constant", ValueDistribution::Kind::Constant, "Always a."},
        {"uniform", ValueDistribution::Kind::Uniform, "Uniform between a and b."},
        {"normal", ValueDistribution::Kind::Normal, "Normal with mean a and standard deviation b."},
        {"log_normal", ValueDistribution::Kind::LogNormal,
         "Log-normal with median a and standard deviation b of the natural logarithm."},
    }};
};

template <> struct EnumNames<RimRoughnessModel> {
    static constexpr std::array<EnumEntry<RimRoughnessModel>, 2> kValues{{
        {"power_law", RimRoughnessModel::PowerLaw,
         "Rim harmonics with amplitudes falling as a power of the harmonic number, of a given RMS: "
         "a simple, configurable spectrum."},
        {"du_2024", RimRoughnessModel::Du2024,
         "The fitted rim-crest spectra of fresh lunar craters of Du et al. (2024), from each "
         "crater's diameter."},
    }};
};

template <> struct EnumNames<DiffusivityModel> {
    static constexpr std::array<EnumEntry<DiffusivityModel>, 5> kValues{{
        {"fassett_2022_hartmann", DiffusivityModel::Fassett2022Hartmann,
         "Lunar size-dependent diffusivity of Fassett et al. (2022) for the Hartmann equilibrium, "
         "3.6 D^0.87 m^2/Myr with D in km."},
        {"fassett_2022_trask", DiffusivityModel::Fassett2022Trask,
         "Lunar size-dependent diffusivity of Fassett et al. (2022) for the Trask equilibrium, "
         "3.1 D^1.05 m^2/Myr with D in km."},
        {"fassett_thomson_2014", DiffusivityModel::FassettThomson2014,
         "Lunar diffusivity of Fassett and Thomson (2014), 5.5 m^2/Myr for every crater size."},
        {"fassett_thomson_2014_calibration", DiffusivityModel::FassettThomson2014Calibration,
         "Fassett and Thomson's (2014) lunar diffusion age against crater age, independent of "
         "size; suits kilometre-scale craters."},
        {"power_law", DiffusivityModel::PowerLaw,
         "A diffusivity of your own, coefficient (D / 1 km)^exponent m^2/Myr, from the power_law "
         "settings."},
    }};
};

template <> struct EnumNames<Body> {
    static constexpr std::array<EnumEntry<Body>, 3> kValues{{
        {"moon", Body::Moon, "The Moon, mean radius 1737.4 km."},
        {"mars", Body::Mars, "Mars, mean radius 3389.5 km."},
        {"mercury", Body::Mercury, "Mercury, mean radius 2439.7 km."},
    }};
};

template <> struct EnumNames<CraterBody> {
    static constexpr std::array<EnumEntry<CraterBody>, 2> kValues{{
        {"moon", CraterBody::Moon,
         "Craters shaped for the Moon: lunar fresh morphometry and complex crater shape."},
        {"mars", CraterBody::Mars,
         "Craters shaped for Mars: Martian fresh morphometry and complex crater shape."},
    }};
};

template <> struct EnumNames<BoulderRelief> {
    static constexpr std::array<EnumEntry<BoulderRelief>, 3> kValues{{
        {"dome", BoulderRelief::Dome,
         "Each boulder's exposed part drawn as a dome: cheap, and enough for small boulders."},
        {"shape", BoulderRelief::Shape,
         "The top surface of each boulder's 3D boulder shape drawn into the heights."},
        {"none", BoulderRelief::None,
         "No boulder relief in the heights, for programs that place every boulder as a model of "
         "its "
         "own."},
    }};
};

template <> struct EnumNames<CraterListFormat> {
    static constexpr std::array<EnumEntry<CraterListFormat>, 2> kValues{{
        {"csv", CraterListFormat::Csv, "Comma-separated values, one row per item."},
        {"json", CraterListFormat::Json, "A JSON document."},
    }};
};

template <> struct EnumNames<ResampleMethod> {
    static constexpr std::array<EnumEntry<ResampleMethod>, 3> kValues{{
        {"nearest", ResampleMethod::Nearest,
         "The value of the nearest cell centre: blocky, but never mixes values."},
        {"bilinear", ResampleMethod::Bilinear,
         "Linear in each axis over the four surrounding cells."},
        {"cubic", ResampleMethod::Cubic,
         "Keys cubic convolution over the sixteen surrounding cells: the smoothest, for upscaling "
         "DEMs."},
    }};
};

template <> struct EnumNames<TiffCompression> {
    static constexpr std::array<EnumEntry<TiffCompression>, 4> kValues{{
        {"none", TiffCompression::None, "Uncompressed: the largest files."},
        {"lzw", TiffCompression::Lzw, "Lossless LZW compression, widely supported."},
        {"deflate", TiffCompression::Deflate, "Lossless Deflate (zlib) compression."},
        {"packbits", TiffCompression::PackBits,
         "PackBits run-length compression; read only, so outputs cannot use it."},
    }};
};

template <typename E>
concept NamedEnum = std::is_enum_v<E> && requires { EnumNames<E>::kValues; };

template <typename E> std::string_view EntryName(const EnumEntry<E>& entry) {
    return entry.name;
}

template <typename E> E EntryValue(const EnumEntry<E>& entry) {
    return entry.value;
}

template <typename E> std::string_view EntryDescription(const EnumEntry<E>& entry) {
    return entry.description;
}

template <typename Spec> struct SpecName;
template <> struct SpecName<FlatSourceSpec> {
    static constexpr std::string_view kName = "flat";
    static constexpr std::string_view kDescription =
        "A level datum over a grid of square cells, to build a synthetic terrain on from scratch.";
};
template <> struct SpecName<RasterSourceSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "A heightmap read from a GeoTIFF in its own projected coordinates, optionally resampled, "
        "to add relief to a real DEM.";
};
template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
    static constexpr std::string_view kDescription =
        "Several sources tried in order, each filling where those before it have no data; the "
        "terrain covers the first source's grid.";
};
template <> struct SpecName<NoiseLayerSpec> {
    static constexpr std::string_view kName = "noise";
    static constexpr std::string_view kDescription =
        "A stack of fractal noise layers added to the terrain, for hills, ridges and rolling "
        "relief.";
};
template <> struct SpecName<CraterLayerSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Impact craters generated from a density and age model, or placed by hand, and cut into "
        "the terrain.";
};
template <> struct SpecName<BoulderLayerSpec> {
    static constexpr std::string_view kName = "boulders";
    static constexpr std::string_view kDescription =
        "Boulders scattered over the ground from a size distribution, in patches and around fresh "
        "craters, as relief or 3D models.";
};
template <> struct SpecName<RoughnessLayerSpec> {
    static constexpr std::string_view kName = "roughness";
    static constexpr std::string_view kDescription =
        "Fine roughness from octaves of noise, scaled to a given RMS slope over a baseline.";
};
template <> struct SpecName<GolombekRappBouldersSpec> {
    static constexpr std::string_view kName = "golombek_rapp";
    static constexpr std::string_view kDescription =
        "Golombek and Rapp boulder sizes, set by the fraction of ground the boulders cover and the "
        "layer's qa and qb.";
};
template <> struct SpecName<Sp8023IntercraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_intercrater";
    static constexpr std::string_view kDescription =
        "Blocks between craters from NASA SP-8023, the lunar surface design model (Figure 7).";
};
template <> struct SpecName<Sp8023CraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_crater";
    static constexpr std::string_view kDescription =
        "Blocks around blocky craters from NASA SP-8023 (Figure 9), for the ejecta of craters "
        "that reached bedrock.";
};
template <> struct SpecName<SynthterrainBouldersSpec> {
    static constexpr std::string_view kName = "synthterrain";
    static constexpr std::string_view kDescription =
        "One of the power-law boulder distributions synthterrain offers, for comparison with it.";
};
template <> struct SpecName<SecondaryLayerSpec> {
    static constexpr std::string_view kName = "secondaries";
    static constexpr std::string_view kDescription =
        "Secondary craters around the craters of the last crater layer before this one, after "
        "Singer et al. (2020).";
};
template <> struct SpecName<BedformLayerSpec> {
    static constexpr std::string_view kName = "bedforms";
    static constexpr std::string_view kDescription =
        "Fields of wind-blown ripples or dunes added to the terrain.";
};
template <> struct SpecName<ScaleLayerSpec> {
    static constexpr std::string_view kName = "scale";
    static constexpr std::string_view kDescription =
        "Scales and shifts the heights so far, for vertical exaggeration or a datum shift.";
};
template <> struct SpecName<ClampLayerSpec> {
    static constexpr std::string_view kName = "clamp";
    static constexpr std::string_view kDescription =
        "Clamps the heights so far between optional bounds, to flatten a basin floor or cap "
        "peaks.";
};
template <> struct SpecName<RegionLayerSpec> {
    static constexpr std::string_view kName = "region";
    static constexpr std::string_view kDescription =
        "Another layer applied only inside a rectangle or circle, feathered outside it: to "
        "confine craters to an area or flatten a landing pad.";
};
template <> struct SpecName<RectangleRegionSpec> {
    static constexpr std::string_view kName = "rectangle";
    static constexpr std::string_view kDescription =
        "An axis-aligned rectangle in the surface's coordinates.";
};
template <> struct SpecName<CircleRegionSpec> {
    static constexpr std::string_view kName = "circle";
    static constexpr std::string_view kDescription =
        "A circle in the surface's coordinates, its radius in metres.";
};
template <> struct SpecName<RimSmoothing> {
    static constexpr std::string_view kName = "rim_smoothing";
    static constexpr std::string_view kDescription =
        "Rounds the sharp rims of placed craters by blending towards a Gaussian-smoothed copy "
        "around each rim.";
};
template <> struct SpecName<HeightmapOutput> {
    static constexpr std::string_view kName = "heightmap";
    static constexpr std::string_view kDescription =
        "Writes the generated heights as a float32 GeoTIFF, with a raster source's projection.";
};
template <> struct SpecName<LunarSurfaceSpec> {
    static constexpr std::string_view kName = "lunar_surface";
    static constexpr std::string_view kDescription =
        "Crater density of a lunar surface of a given age as the DSNE builds it: Neukum "
        "production capped by the Trask equilibrium.";
};
template <> struct SpecName<MarsSurfaceSpec> {
    static constexpr std::string_view kName = "mars_surface";
    static constexpr std::string_view kDescription =
        "Crater density of a Martian surface of a given age, from Ivanov's production function "
        "and chronology, with no erosion.";
};
template <> struct SpecName<PowerLawSpec> {
    static constexpr std::string_view kName = "power_law";
    static constexpr std::string_view kDescription =
        "A cumulative power law, N(>=D) = coefficient D^-exponent per m^2 with D in metres.";
};
template <> struct SpecName<PiecewisePowerLawSpec> {
    static constexpr std::string_view kName = "piecewise_power_law";
    static constexpr std::string_view kDescription =
        "A cumulative distribution made of power-law segments over diameter ranges.";
};
template <> struct SpecName<TabulatedSpec> {
    static constexpr std::string_view kName = "tabulated";
    static constexpr std::string_view kDescription =
        "A cumulative distribution from a table of diameters and counts N(>=D), in units you "
        "choose.";
};
template <> struct SpecName<TraskSpec> {
    static constexpr std::string_view kName = "trask";
    static constexpr std::string_view kDescription =
        "The Trask (1966) lunar equilibrium density, N(>=D) = 10^-1.1 D^-2.";
};
template <> struct SpecName<HartmannSpec> {
    static constexpr std::string_view kName = "hartmann";
    static constexpr std::string_view kDescription =
        "The Hartmann (1984) lunar equilibrium density, N(>=D) = 10^-1.14 D^-1.83 per km^2 with D "
        "in km.";
};
template <> struct SpecName<MooreSpec> {
    static constexpr std::string_view kName = "moore";
    static constexpr std::string_view kDescription =
        "Moore's terrain-specific crater distributions for Lunar Roving Vehicle trafficability.";
};
template <> struct SpecName<ViperSpec> {
    static constexpr std::string_view kName = "viper";
    static constexpr std::string_view kDescription =
        "The VIPER environmental specification crater density, as synthterrain uses it; kept for "
        "comparison with synthterrain.";
};
template <> struct SpecName<AutoAgesSpec> {
    static constexpr std::string_view kName = "auto";
    static constexpr std::string_view kDescription =
        "The ages of a lunar_surface or mars_surface density, otherwise ages uniform over a 1 Gyr "
        "lifetime.";
};
template <> struct SpecName<FreshAgesSpec> {
    static constexpr std::string_view kName = "fresh";
    static constexpr std::string_view kDescription =
        "Every crater is fresh, with age and lifetime zero.";
};
template <> struct SpecName<UniformAgesSpec> {
    static constexpr std::string_view kName = "uniform";
    static constexpr std::string_view kDescription =
        "Ages uniform over one lifetime for every diameter, so freshness is uniform between 0 "
        "and 1.";
};
template <> struct SpecName<SteadyStateAgesSpec> {
    static constexpr std::string_view kName = "steady_state";
    static constexpr std::string_view kDescription =
        "Lunar crater ages where small craters are in equilibrium, from the Neukum production "
        "and an equilibrium density.";
};
template <> struct SpecName<FreshnessDegradationSpec> {
    static constexpr std::string_view kName = "freshness";
    static constexpr std::string_view kDescription =
        "Scales depth and rim height linearly with freshness, down to a cutoff depth ratio at the "
        "end of a crater's lifetime.";
};
template <> struct SpecName<DiffusionDegradationSpec> {
    static constexpr std::string_view kName = "diffusion";
    static constexpr std::string_view kDescription =
        "Wears craters down by topographic diffusion, from each crater's diameter and age.";
};
template <> struct SpecName<NoDegradationSpec> {
    static constexpr std::string_view kName = "none";
    static constexpr std::string_view kDescription =
        "Craters stay fresh, optionally all with one depth to diameter ratio.";
};
template <> struct SpecName<FassettThomsonSpec> {
    static constexpr std::string_view kName = "fassett_thomson";
    static constexpr std::string_view kDescription =
        "Fresh simple-crater profile of Fassett and Thomson (2014), with synthterrain's variants.";
};
template <> struct SpecName<ComplexProfileSpec> {
    static constexpr std::string_view kName = "complex";
    static constexpr std::string_view kDescription =
        "A simple profile below the body's simple-to-complex transition, and complex craters "
        "with flat floors and central peaks above it.";
};
template <> struct SpecName<MartinParkesDunstanSpec> {
    static constexpr std::string_view kName = "martin_parkes_dunstan";
    static constexpr std::string_view kDescription =
        "Simple-crater profile of Martin, Parkes and Dunstan (2014), developed for the PANGU "
        "simulator; degradation rounds its rim.";
};
template <> struct SpecName<BoulderListOutput> {
    static constexpr std::string_view kName = "boulder_list";
    static constexpr std::string_view kDescription =
        "Writes every boulder the layers placed, models and relief alike, as CSV or JSON.";
};
template <> struct SpecName<BoulderModelsOutput> {
    static constexpr std::string_view kName = "boulder_models";
    static constexpr std::string_view kDescription =
        "Writes the boulders placed as 3D models on the ground, as a glTF scene (.glb) or one "
        "merged "
        "PLY mesh (.ply).";
};
template <> struct SpecName<CraterListOutput> {
    static constexpr std::string_view kName = "crater_list";
    static constexpr std::string_view kDescription =
        "Writes the generated and manual craters as CSV or JSON.";
};
template <> struct SpecName<PlaneSurfaceSpec> {
    static constexpr std::string_view kName = "plane";
    static constexpr std::string_view kDescription =
        "A flat projected frame in metres, for local sites.";
};
template <> struct SpecName<SphereSurfaceSpec> {
    static constexpr std::string_view kName = "sphere";
    static constexpr std::string_view kDescription =
        "A whole body such as a planet or moon: positions in longitude and latitude (degrees) and "
        "features generated over the whole body.";
};
template <> struct SpecName<BodyMeshOutput> {
    static constexpr std::string_view kName = "body_mesh";
    static constexpr std::string_view kDescription =
        "Writes a mesh of the whole body on a sphere, from six cube faces, as glTF (.glb) or PLY "
        "(.ply).";
};
template <> struct SpecName<AlbedoOutput> {
    static constexpr std::string_view kName = "albedo";
    static constexpr std::string_view kDescription =
        "Writes the albedo as a float32 GeoTIFF of one band or three (red, green, blue).";
};
template <> struct SpecName<SyntheticAlbedoSpec> {
    static constexpr std::string_view kName = "synthetic";
    static constexpr std::string_view kDescription =
        "A base colour in hue, saturation and value, each varied by noise: an albedo for terrain "
        "without data.";
};
template <> struct SpecName<RasterAlbedoSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "Albedo read from one or three bands of a GeoTIFF in the terrain's projection, such as an "
        "orbital albedo map.";
};
template <> struct SpecName<CraterAlbedoSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Bright ejecta, interiors and rays of the craters of a crater layer, fading with crater "
        "age.";
};

/// Reads the keys of one JSON object into fields, remembering which keys were asked for so that
/// any other key can be reported.
class Reader {
  public:
    Reader(const Json& json, std::string path) : json_(json), path_(std::move(path)) {
        if (!json_.is_object()) {
            Fail(path_, "expected an object, not " + TypeName(json_));
        }
    }

    /// Reads `key` into `value` when present; `description` and `unit` are for `ConfigSchema`
    /// only.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {});

    /// Marks `key` as known without reading it.
    void Accept(std::string_view key) {
        known_.emplace(key);
    }

    bool Has(std::string_view key) const {
        return json_.contains(key);
    }

    /// @throws ConfigError for a key that no field asked for.
    void Finish() const {
        for (const auto& [key, value] : json_.items()) {
            if (known_.count(key) == 0) {
                std::string expected;
                for (const std::string& name : known_) {
                    expected += (expected.empty() ? "" : ", ") + name;
                }
                Fail(Child(path_, key), "unknown key; expected one of: " + expected);
            }
        }
    }

  private:
    const Json& json_;
    std::string path_;
    std::set<std::string, std::less<>> known_;
};

/// Writes fields as the keys of a JSON object, in order.
class Writer {
  public:
    explicit Writer(Json& json) : json_(json) {}

    /// Writes `value` as `key`; `description` and `unit` are for `ConfigSchema` only.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {});

  private:
    Json& json_;
};

template <typename IO> void Fields(IO& io, FlatSourceSpec& source);
template <typename IO> void Fields(IO& io, RasterSourceSpec& source);
template <typename IO> void Fields(IO& io, ChainSourceSpec& source);
template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer);
template <typename IO> void Fields(IO& io, HeightmapOutput& output);
template <typename IO> void Fields(IO& io, NoiseLayerConfig& layer);
template <typename IO> void Fields(IO& io, PowerLawSegment& segment);
template <typename IO> void Fields(IO& io, CraterSpacing& spacing);
template <typename IO> void Fields(IO& io, Crater& crater);
template <typename IO> void Fields(IO& io, CraterBlend& blend);
template <typename IO> void Fields(IO& io, ResolutionFade& fade);
template <typename IO> void Fields(IO& io, CraterApplyOptions& apply);
template <typename IO> void Fields(IO& io, ValueDistribution& distribution);
template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity);
template <typename IO> void Fields(IO& io, RimSmoothing& smoothing);
template <typename IO> void Fields(IO& io, CraterLayerSpec& craters);
template <typename IO> void Fields(IO& io, BoulderPatches& patches);
template <typename IO> void Fields(IO& io, BoulderEjecta& ejecta);
template <typename IO> void Fields(IO& io, ExposureParams& exposure);
template <typename IO> void Fields(IO& io, BoulderLayerSpec& spec);
template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec);
template <typename IO> void Fields(IO& io, GolombekRappBouldersSpec& spec);
template <typename IO> void Fields(IO& io, Sp8023IntercraterBouldersSpec& spec);
template <typename IO> void Fields(IO& io, Sp8023CraterBouldersSpec& spec);
template <typename IO> void Fields(IO& io, SynthterrainBouldersSpec& spec);
template <typename IO> void Fields(IO& io, ScaleLayerSpec& spec);
template <typename IO> void Fields(IO& io, BedformLayerSpec& spec);
template <typename IO> void Fields(IO& io, SecondaryLayerSpec& spec);
template <typename IO> void Fields(IO& io, ClampLayerSpec& spec);
template <typename IO> void Fields(IO& io, RectangleRegionSpec& spec);
template <typename IO> void Fields(IO& io, CircleRegionSpec& spec);
template <typename IO> void Fields(IO& io, RegionLayerSpec& spec);
template <typename IO> void Fields(IO& io, LunarSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, PowerLawSpec& spec);
template <typename IO> void Fields(IO& io, MarsSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, PiecewisePowerLawSpec& spec);
template <typename IO> void Fields(IO& io, TabulatedSpec& spec);
template <typename IO> void Fields(IO& io, TraskSpec& spec);
template <typename IO> void Fields(IO& io, HartmannSpec& spec);
template <typename IO> void Fields(IO& io, MooreSpec& spec);
template <typename IO> void Fields(IO& io, ViperSpec& spec);
template <typename IO> void Fields(IO& io, AutoAgesSpec& spec);
template <typename IO> void Fields(IO& io, FreshAgesSpec& spec);
template <typename IO> void Fields(IO& io, UniformAgesSpec& spec);
template <typename IO> void Fields(IO& io, SteadyStateAgesSpec& spec);
template <typename IO> void Fields(IO& io, FreshnessDegradationSpec& spec);
template <typename IO> void Fields(IO& io, NoDegradationSpec& spec);
template <typename IO> void Fields(IO& io, DiffusionDegradationSpec& spec);
template <typename IO> void Fields(IO& io, PowerLawDiffusivity& law);
template <typename IO> void Fields(IO& io, FassettThomsonSpec& spec);
template <typename IO> void Fields(IO& io, MartinParkesDunstanSpec& spec);
template <typename IO> void Fields(IO& io, ComplexProfileSpec& spec);
template <typename IO> void Fields(IO& io, CraterListOutput& output);
template <typename IO> void Fields(IO& io, BoulderListOutput& output);
template <typename IO> void Fields(IO& io, BoulderModelsOutput& output);
template <typename IO> void Fields(IO& io, BoulderModelThreshold& models);
template <typename IO> void Fields(IO& io, ValueRange& range);
template <typename IO> void Fields(IO& io, BoulderStyle& style);
template <typename IO> void Fields(IO& io, BoulderShapeOptions& shapes);
template <typename IO> void Fields(IO& io, AlbedoOutput& output);
template <typename IO> void Fields(IO& io, BodyMeshOutput& output);
template <typename IO> void Fields(IO& io, PlaneSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, SphereSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, SyntheticAlbedoSpec& spec);
template <typename IO> void Fields(IO& io, AlbedoDetail& detail);
template <typename IO> void Fields(IO& io, RasterAlbedoSpec& spec);
template <typename IO> void Fields(IO& io, CraterRays& rays);
template <typename IO> void Fields(IO& io, ObliqueImpacts& oblique);
template <typename IO> void Fields(IO& io, RayRetention& retention);
template <typename IO> void Fields(IO& io, CraterAlbedoOptions& options);
template <typename IO> void Fields(IO& io, CraterAlbedoSpec& spec);
template <typename IO> void Fields(IO& io, AlbedoSpec& spec);

template <typename T>
concept HasFields = requires(Reader& reader, T& value) { Fields(reader, value); };

void Read(const Json& json, const std::string& path, double& value) {
    if (!json.is_number()) {
        Fail(path, "expected a number, not " + TypeName(json));
    }
    value = json.get<double>();
    if (!std::isfinite(value)) {
        Fail(path, "expected a finite number");
    }
}

void Read(const Json& json, const std::string& path, float& value) {
    double number = 0.0;
    Read(json, path, number);
    if (std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max())) {
        Fail(path, "number is too large");
    }
    value = static_cast<float>(number);
}

void Read(const Json& json, const std::string& path, bool& value) {
    if (!json.is_boolean()) {
        Fail(path, "expected true or false, not " + TypeName(json));
    }
    value = json.get<bool>();
}

void Read(const Json& json, const std::string& path, std::string& value) {
    if (!json.is_string()) {
        Fail(path, "expected a string, not " + TypeName(json));
    }
    value = json.get<std::string>();
}

void Read(const Json& json, const std::string& path, int& value) {
    if (!json.is_number_integer()) {
        Fail(path, "expected an integer, not " + TypeName(json));
    }
    if (json.is_number_unsigned() &&
        json.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        Fail(path, "integer is too large");
    }
    const auto number = json.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        Fail(path, "integer is out of range");
    }
    value = static_cast<int>(number);
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
void Read(const Json& json, const std::string& path, T& value) {
    if (!json.is_number_integer()) {
        Fail(path, "expected a non-negative integer, not " + TypeName(json));
    }
    if (!json.is_number_unsigned()) {
        Fail(path, "expected a non-negative integer");
    }
    const auto number = json.get<std::uint64_t>();
    if (number > std::numeric_limits<T>::max()) {
        Fail(path, "integer is too large");
    }
    value = static_cast<T>(number);
}

template <NamedEnum E> void Read(const Json& json, const std::string& path, E& value) {
    std::string name;
    Read(json, path, name);
    std::string expected;
    for (const auto& entry : EnumNames<E>::kValues) {
        if (EntryName(entry) == name) {
            value = EntryValue(entry);
            return;
        }
        expected += (expected.empty() ? "" : ", ") + std::string(EntryName(entry));
    }
    Fail(path, "unknown value \"" + name + "\"; expected one of: " + expected);
}

template <typename T> void Read(const Json& json, const std::string& path, std::optional<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value);
template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value);

template <typename T> void AfterRead(const Json& /*json*/, T& /*value*/) {}

void AfterRead(const Json& /*json*/, Crater& crater) {
    crater.manual = true;
}

/// The list format a path's extension names when the configuration gives none.
template <typename Output> void InferListFormat(const Json& json, Output& output) {
    if (!json.contains("format")) {
        const std::string& path = output.path;
        const bool isJson = path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0;
        output.format = isJson ? CraterListFormat::Json : CraterListFormat::Csv;
    }
}

void AfterRead(const Json& json, CraterListOutput& output) {
    InferListFormat(json, output);
}

void AfterRead(const Json& json, BoulderListOutput& output) {
    InferListFormat(json, output);
}

template <HasFields T> void Read(const Json& json, const std::string& path, T& value) {
    Reader reader(json, path);
    Fields(reader, value);
    reader.Finish();
    AfterRead(json, value);
}

template <typename T>
void Read(const Json& json, const std::string& path, std::optional<T>& value) {
    if (json.is_null()) {
        value.reset();
        return;
    }
    T inner = value.value_or(T{});
    Read(json, path, inner);
    value = std::move(inner);
}

template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value) {
    Read(json, path, *value);
}

template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value) {
    if (!json.is_array()) {
        Fail(path, "expected an array, not " + TypeName(json));
    }
    std::vector<T> items(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        Read(json[i], Element(path, i), items[i]);
    }
    value = std::move(items);
}

template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value) {
    if (!json.is_object()) {
        Fail(path, "expected an object with a \"type\", not " + TypeName(json));
    }
    const auto type = json.find("type");
    std::string expected;
    ((expected += (expected.empty() ? "" : ", ") + std::string(SpecName<Ts>::kName)), ...);
    if (type == json.end() || !type->is_string()) {
        Fail(Child(path, "type"), "expected a string, one of: " + expected);
    }
    const std::string name = type->template get<std::string>();
    bool found = false;
    const auto tryAlternative = [&]<typename T>(T* /*tag*/) {
        if (found || name != SpecName<T>::kName) {
            return;
        }
        T spec{};
        Reader reader(json, path);
        reader.Accept("type");
        Fields(reader, spec);
        reader.Finish();
        AfterRead(json, spec);
        value = std::move(spec);
        found = true;
    };
    (tryAlternative(static_cast<Ts*>(nullptr)), ...);
    if (!found) {
        Fail(Child(path, "type"), "unknown type \"" + name + "\"; expected one of: " + expected);
    }
}

template <typename T>
void Reader::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/) {
    known_.emplace(key);
    const auto found = json_.find(key);
    if (found != json_.end()) {
        Read(*found, Child(path_, key), value);
    }
}

Json ToJson(double value) {
    return value;
}

Json ToJson(float value) {
    return static_cast<double>(value);
}

Json ToJson(bool value) {
    return value;
}

Json ToJson(int value) {
    return value;
}

Json ToJson(const std::string& value) {
    return value;
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
Json ToJson(T value) {
    return static_cast<std::uint64_t>(value);
}

template <NamedEnum E> Json ToJson(E value) {
    for (const auto& entry : EnumNames<E>::kValues) {
        if (EntryValue(entry) == value) {
            return std::string(EntryName(entry));
        }
    }
    return nullptr;
}

template <typename T> Json ToJson(const std::optional<T>& value);
template <typename T> Json ToJson(const Boxed<T>& value);
template <typename T> Json ToJson(const std::vector<T>& value);
template <typename... Ts> Json ToJson(const std::variant<Ts...>& value);

template <HasFields T> Json ToJson(const T& value) {
    Json json = Json::object();
    T copy = value;
    Writer writer(json);
    Fields(writer, copy);
    return json;
}

template <typename T> Json ToJson(const std::optional<T>& value) {
    return value ? ToJson(*value) : Json(nullptr);
}

template <typename T> Json ToJson(const Boxed<T>& value) {
    return ToJson(*value);
}

template <typename T> Json ToJson(const std::vector<T>& value) {
    Json json = Json::array();
    for (const T& item : value) {
        json.push_back(ToJson(item));
    }
    return json;
}

template <typename... Ts> Json ToJson(const std::variant<Ts...>& value) {
    return std::visit(
        [](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            Json json = Json::object();
            json["type"] = std::string(SpecName<Spec>::kName);
            const Json fields = ToJson(spec);
            for (const auto& item : fields.items()) {
                json[item.key()] = item.value();
            }
            return json;
        },
        value);
}

template <typename T>
void Writer::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/) {
    json_[std::string(key)] = ToJson(value);
}

template <typename IO> void Fields(IO& io, FlatSourceSpec& source) {
    io("width", source.width, "Columns of the grid.");
    io("height", source.height, "Rows of the grid.");
    io("pixel_size", source.pixelSize,
       "Cell size in metres; on a sphere, metres along a meridian, the same in degrees of "
       "longitude.",
       "m");
    io("elevation", source.elevation, "Height of the level datum, in metres.", "m");
    io("centre_x", source.centreX,
       "Centre of the grid along x in surface coordinates; the longitude in degrees on a sphere.");
    io("centre_y", source.centreY,
       "Centre of the grid along y in surface coordinates; the latitude in degrees on a sphere.");
}

template <typename IO> void Fields(IO& io, RasterSourceSpec& source) {
    io("path", source.path,
       "GeoTIFF to read the heights from; relative paths resolve against the configuration's "
       "folder.");
    io("band", source.band, "Band of the file to read, counting from 0.");
    io("pixel_size", source.pixelSize,
       "Cell size in metres to resample the raster to over the same extent, as when upscaling a "
       "DEM. Unset keeps the file's.",
       "m");
    io("resampling", source.resampling, "Interpolation used when resampling to the cell size.");
    io("scale", source.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset", source.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
}

template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources,
       "Sources tried in order, each filling where those before it have no data; the terrain "
       "covers the first source's grid.");
}

template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer) {
    io("stack", layer.stack,
       "Noise layers, 1 to 8, combined in order: the first enabled layer sets the height and each "
       "later one is combined with it by its blend mode.");
}

template <typename IO> void Fields(IO& io, HeightmapOutput& output) {
    io("path", output.path,
       "GeoTIFF file to write; relative paths resolve against the output folder (the CLI's "
       "--output-dir), else the configuration's folder.");
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size", output.tileSize,
       "Width and height of the file's tiles in pixels; a positive multiple of 16.", "px");
}

template <typename IO> void Fields(IO& io, NoiseLayerConfig& config) {
    NoiseLayer& layer = config.layer;
    FractalParams& fractal = layer.fractal;
    io("enabled", layer.enabled, "Includes this layer in the stack; disabled layers are skipped.");
    io("basis", fractal.basis, "Single-octave noise the fractal is built from.");
    io("fractal", fractal.type, "How the octaves of the basis noise are combined.");
    io("blend", layer.blend,
       "How this layer combines with the height of the layers before it; the first enabled layer "
       "sets the height instead.");
    io("seed", fractal.seed,
       "Seed of the noise; changing it gives a different pattern of the same character.");
    io("wavelength", fractal.wavelength,
       "Wavelength of the first, broadest octave, in metres; sets the size of the largest "
       "features.",
       "m");
    io("amplitude", fractal.amplitude, "Largest height of the layer above or below 0, in metres.",
       "m");
    io("octaves", fractal.octaves,
       "Number of octaves, at least 1; each one adds detail at a shorter wavelength.");
    io("lacunarity", fractal.lacunarity,
       "Frequency ratio between successive octaves; greater than 1.");
    io("gain", fractal.gain,
       "Weight ratio between successive octaves, greater than 0; higher values give rougher "
       "terrain. Unused when the Hurst exponent is set.");
    io("hurst", config.hurst,
       "Hurst exponent that sets the octave gain to lacunarity^-hurst: near 1 is smooth, lower is "
       "rougher at short baselines. Unset uses the gain.");
    io("offset", fractal.offset,
       "Ridge offset of the ridged and hybrid multifractal types; greater than 0.");
    io("warp_amplitude", layer.warpAmplitude,
       "Domain warp displacement in metres, which bends the noise into swirled shapes; 0 "
       "disables warping.",
       "m");
    io("warp_wavelength", layer.warpWavelength, "Wavelength of the warp field, in metres.", "m");
}

template <typename IO> void Fields(IO& io, PowerLawSegment& segment) {
    io("start", segment.start,
       "Diameter in metres where this segment begins; it runs up to the next segment's start.",
       "m");
    io("coefficient", segment.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "metres.");
    io("exponent", segment.exponent,
       "Exponent of the segment's power law; larger values give relatively more small craters.");
}

template <typename IO> void Fields(IO& io, CraterSpacing& spacing) {
    io("diameter_ratio", spacing.diameterRatio,
       "Largest ratio of two craters' diameters, at least 1, for them to count as similar in size "
       "and be kept apart.");
    io("separation", spacing.separation,
       "Least distance between the centres of similar-size craters, in larger diameters; 0 "
       "disables spacing.");
}

template <typename IO> void Fields(IO& io, Crater& crater) {
    io("x", crater.x,
       "Centre along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("y", crater.y,
       "Centre along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("diameter", crater.diameter, "Rim-to-rim diameter, in metres.", "m");
    io("age_gyr", crater.ageGyr,
       "Age in Gyr; older craters are more degraded under a degradation model and are applied "
       "earlier in oldest-first order.",
       "Gyr");
    io("id", crater.id,
       "Stable ID that seeds the crater's outline; 0 derives one from its position and diameter.");
}

template <typename IO> void Fields(IO& io, ResolutionFade& fade) {
    io("start_samples", fade.startSamples,
       "Samples across a feature's diameter up to which it is left out entirely.");
    io("end_samples", fade.endSamples,
       "Samples across a feature's diameter from which it is applied in full; it fades in between "
       "the two counts.");
    io("shape", fade.shape, "Curve of the fade between the start and end sample counts.");
}

template <typename IO> void Fields(IO& io, CraterBlend& blend) {
    io("start_radii", blend.startRadii,
       "Distance from the centre, in crater radii, up to which the interior is fully replaced.");
    io("end_radii", blend.endRadii,
       "Distance from the centre, in crater radii, from which the surrounding terrain is draped "
       "unchanged.");
    io("shape", blend.shape, "Curve of the transition from the replaced interior to the exterior.");
}

template <typename IO> void Fields(IO& io, CraterApplyOptions& apply) {
    io("order", apply.order,
       "Order in which overlapping craters are applied; later craters overprint earlier ones.");
    io("fade", apply.fade, "How craters too small to resolve at the sample spacing fade out.");
    io("datum", apply.datum,
       "Reference surface each crater's interior is cut relative to, fitted to the ground beneath "
       "it; none adds craters onto the terrain everywhere.");
    io("datum_fit_radii", apply.datumFitRadii,
       "Radius of the footprint the datum is fitted over, in crater radii.");
    io("interior_replacement", apply.interiorReplacement,
       "Share of the interior replaced by datum plus profile, from 0 to 1: 1 erases older relief "
       "inside the crater, 0 keeps it.");
    io("blend", apply.blend,
       "Where and how a crater's replaced interior blends into the surrounding terrain.");
    io("irregularity", apply.irregularity,
       "Irregular, non-circular crater outlines: which craters have them and how their shapes "
       "are drawn.");
}

template <typename IO> void Fields(IO& io, ValueDistribution& distribution) {
    io("kind", distribution.kind, "Kind of distribution the value is drawn from.");
    io("a", distribution.a,
       "The constant value, the uniform's lower bound, the normal's mean or the log-normal's "
       "median.");
    io("b", distribution.b,
       "The uniform's upper bound, or the standard deviation (of the natural logarithm for a "
       "log-normal); unused for a constant.");
    std::optional<double> min;
    std::optional<double> max;
    if (std::isfinite(distribution.min)) {
        min = distribution.min;
    }
    if (std::isfinite(distribution.max)) {
        max = distribution.max;
    }
    io("min", min, "Lower bound drawn values are clamped to; unset leaves them unbounded below.");
    io("max", max,
       "Upper bound drawn values are clamped to; unset leaves them unbounded above, which normal "
       "and log-normal kinds do not allow.");
    distribution.min = min.value_or(-std::numeric_limits<double>::infinity());
    distribution.max = max.value_or(std::numeric_limits<double>::infinity());
}

template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity) {
    io("enabled", irregularity.enabled,
       "Gives eligible craters irregular, non-circular outlines; off keeps every crater round.");
    io("seed", irregularity.seed,
       "Seed the outlines are drawn with, along with each crater's ID; changing it reshapes every "
       "irregular crater.");
    io("min_diameter", irregularity.minDiameter,
       "Smallest diameter, in metres, of craters made irregular.", "m");
    std::optional<double> maxDiameter;
    if (std::isfinite(irregularity.maxDiameter)) {
        maxDiameter = irregularity.maxDiameter;
    }
    io("max_diameter", maxDiameter,
       "Largest diameter, in metres, of craters made irregular; unset has no upper limit.", "m");
    irregularity.maxDiameter = maxDiameter.value_or(std::numeric_limits<double>::infinity());
    io("min_age_gyr", irregularity.minAgeGyr, "Youngest age, in Gyr, of craters made irregular.",
       "Gyr");
    std::optional<double> maxAge;
    if (std::isfinite(irregularity.maxAgeGyr)) {
        maxAge = irregularity.maxAgeGyr;
    }
    io("max_age_gyr", maxAge,
       "Oldest age, in Gyr, of craters made irregular; unset has no upper limit.", "Gyr");
    irregularity.maxAgeGyr = maxAge.value_or(std::numeric_limits<double>::infinity());
    io("manual", irregularity.manual,
       "Makes manual craters irregular too; otherwise only generated craters are.");
    io("rough_share", irregularity.roughShare,
       "Share of eligible craters, from 0 to 1, given rough, wavy rims.");
    io("model", irregularity.model, "How rough rims are drawn.");
    io("scatter", irregularity.scatter,
       "With du_2024, scatters each crater's spectrum by the fits' measured misfits and noise.");
    io("max_deviation", irregularity.maxDeviation,
       "With du_2024, the largest deviation of a rim from a circle, in radii; outlines beyond it "
       "are scaled down to it.");
    io("roughness", irregularity.roughness,
       "With power_law, the RMS deviation of a rough rim from a circle, in crater radii, drawn per "
       "crater.");
    io("spectral_slope", irregularity.spectralSlope,
       "With power_law, the exponent s of the rim harmonics' amplitudes, falling as k^-s, drawn "
       "per crater; higher values leave less fine waviness.");
    io("min_harmonic", irregularity.minHarmonic,
       "With power_law, the lowest harmonic of the rim's waviness, from 1 to 64.");
    io("max_harmonic", irregularity.maxHarmonic,
       "With power_law, the highest harmonic of the rim's waviness, at most 64; higher harmonics "
       "add finer wiggles.");
    io("elongated_share", irregularity.elongatedShare,
       "Share of eligible craters, from 0 to 1, made elliptical, keeping their area.");
    io("ellipticity", irregularity.ellipticity,
       "Long over short rim-to-rim diameter of elliptical craters, drawn per crater; at least 1.");
    io("heading", irregularity.heading,
       "Heading of elliptical craters' long axes, in radians counter-clockwise from +x; unset "
       "gives random headings.",
       "rad");
    io("heading_spread", irregularity.headingSpread,
       "Largest random turn of a long axis either way from the heading, in radians.", "rad");
}

template <typename IO> void Fields(IO& io, RimSmoothing& smoothing) {
    io("sigma_radii", smoothing.sigmaRadii,
       "Gaussian standard deviation as a fraction of each crater's radius; larger values round "
       "rims more, and 0 disables smoothing.");
    io("inner_radii", smoothing.innerRadii,
       "Inner edge of the smoothed band around the rim, in crater radii; below 1.");
    io("outer_radii", smoothing.outerRadii,
       "Outer edge of the smoothed band around the rim, in crater radii; above 1.");
    io("shape", smoothing.shape,
       "How the smoothing fades from full at the rim to none at the band's edges.");
    io("strength", smoothing.strength,
       "Fraction of the smoothed height used at the rim, from 0 to 1; lower values keep more of "
       "the sharp crest.");
    io("fade", smoothing.fade,
       "How rims of craters too small to resolve at the sample spacing fade out.");
}

template <typename IO> void Fields(IO& io, CraterLayerSpec& craters) {
    io("body", craters.body,
       "Body the craters are shaped for, which sets their fresh morphometry and complex crater "
       "shape and which densities and ages are allowed.");
    io("generate", craters.generate,
       "Generates craters from the density; off places only the manual craters.");
    io("seed", craters.seed,
       "Seed the craters are generated with; changing it gives a different crater field of the "
       "same statistics.");
    io("density", craters.density,
       "Size-frequency distribution of the generated craters: how many of each diameter per unit "
       "area.");
    io("ages", craters.ages,
       "How generated craters are given ages, which the degradation model wears them down by.");
    io("min_diameter", craters.minDiameter,
       "Smallest crater generated, in metres; lower values add many more small craters.", "m");
    io("max_diameter", craters.maxDiameter,
       "Largest crater generated, in metres; above the smallest.", "m");
    io("spacing", craters.spacing,
       "Keeps craters of similar size apart by removing conflicting ones, which lowers the density "
       "below the model's.");
    io("max_craters", craters.maxCraters,
       "Most generated craters kept, the largest first; 0 keeps all. Manual craters are always "
       "kept, and a cap means tiles no longer match the whole grid.");
    io("manual", craters.manual,
       "Craters placed by hand in the surface's coordinates, added to any generated ones.");
    io("profile", craters.profile,
       "Shape of a fresh crater's cross-section, scaled to each crater's diameter.");
    io("degradation", craters.degradation,
       "How craters wear down from their fresh shape with age.");
    io("apply", craters.apply,
       "How craters are cut into the terrain: their order, datum, blending, fading and irregular "
       "outlines.");
}

template <typename IO> void Fields(IO& io, BoulderPatches& patches) {
    io("spread", patches.spread,
       "Boulder abundance varies from 1 / spread to spread times its mean, at least 1; 1 is the "
       "same "
       "everywhere.");
    io("size", patches.size,
       "Typical width of a patch of richer or poorer boulder cover, in metres.", "m");
}

template <typename IO> void Fields(IO& io, BoulderEjecta& ejecta) {
    io("min_crater_diameter", ejecta.minCraterDiameter,
       "Smallest crater, in metres, that throws blocks out; 0 with no regolith thickness disables "
       "crater blocks.",
       "m");
    io("regolith_thickness", ejecta.regolithThickness,
       "Regolith thickness in metres: craters smaller than it over the excavation depth ratio dig "
       "no boulder and throw no blocks. Unset ignores the regolith.",
       "m");
    io("excavation_depth_ratio", ejecta.excavationDepthRatio,
       "Depth of excavation over crater diameter, in (0, 1]; with the regolith thickness it sets "
       "the smallest crater that throws blocks.");
    io("coverage", ejecta.coverage,
       "Golombek-Rapp k, the fraction of ground covered by blocks at the rim of a fresh crater, "
       "between 0 and 1; used with golombek_rapp ejecta sizes.");
    io("profile", ejecta.profile,
       "How the share of a crater's blocks falls off with distance from its centre.");
    io("reach", ejecta.reach,
       "Distance from the crater's centre, in crater radii, beyond which it adds no blocks; over "
       "1.");
    io("decay_radii", ejecta.decayRadii,
       "With the exponential profile, the e-folding distance of the blocks outside the rim, in "
       "crater radii.");
    io("floor_share", ejecta.floorShare,
       "With the exponential profile, the crater floor's share of the rim's blocks, from 0 to 1.");
    io("aging", ejecta.aging,
       "How a crater's blocks wear away with time, so that few are left around old craters.");
    io("min_freshness", ejecta.minFreshness,
       "With freshness aging, the freshness (current over fresh depth) at or below which a crater "
       "keeps no blocks; from 0 to below 1.");
    io("lifetime_gyr", ejecta.lifetimeGyr,
       "With age aging, the crater age in Gyr at which a crater keeps no blocks.", "Gyr");
    io("aging_exponent", ejecta.agingExponent,
       "With age aging, the exponent of (1 - age / lifetime); higher values strip blocks from "
       "craters sooner.");
    io("gravity", ejecta.gravity,
       "Surface gravity in m/s^2: with the regolith thickness, it limits how far blocks are "
       "thrown, after Bart and Melosh (2010). Unset applies no such limit.");
}

template <typename IO> void Fields(IO& /*io*/, GolombekRappBouldersSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, Sp8023IntercraterBouldersSpec& spec) {
    io("curve", spec.curve,
       "Which of the model's minimum, nominal and maximum curves to use, from fewest to most "
       "boulders.");
}

template <typename IO> void Fields(IO& io, Sp8023CraterBouldersSpec& spec) {
    io("curve", spec.curve,
       "Which of the model's minimum, nominal and maximum curves to use, from fewest to most "
       "boulders.");
}

template <typename IO> void Fields(IO& io, SynthterrainBouldersSpec& spec) {
    io("model", spec.model, "Which of synthterrain's power-law boulder distributions to use.");
}

template <typename IO> void Fields(IO& io, ExposureParams& exposure) {
    io("reach", exposure.reach,
       "Distance in metres around a point at which its surroundings are read, to tell rises from "
       "lows.",
       "m");
    io("rise_soil", exposure.riseSoil,
       "Height above the surroundings, as a share of the reach, at or below which the ground is "
       "soil.");
    io("rise_boulder", exposure.riseBoulder,
       "Height above the surroundings, as a share of the reach, at or above which the ground is "
       "bedrock; above the soil threshold.");
    io("slope_soil", exposure.slopeSoil, "Slope, as rise over run, at or under which soil holds.");
    io("slope_boulder", exposure.slopeBoulder,
       "Slope, as rise over run, at or over which no soil holds and the ground is bedrock; above "
       "the soil threshold.");
    io("floor_cover", exposure.floorCover,
       "Share of level ground that is bedrock too, in noise patches, as on a swept lava floor; "
       "from 0 to below 1, and 0 for none.");
    io("floor_patch", exposure.floorPatch,
       "Width of the bedrock patches on level ground, in metres.", "m");
    io("seed", exposure.seed, "Seed of the bedrock patches on level ground.");
}

template <typename IO> void Fields(IO& io, BoulderLayerSpec& spec) {
    BoulderLayerOptions& boulders = spec.boulders;
    io("coverage", boulders.coverage,
       "Golombek-Rapp k, the fraction of the ground covered by boulders, between 0 and 1; used "
       "with "
       "golombek_rapp sizes, where higher values give more boulders.");
    io("qa", boulders.qa,
       "Constant term of the Golombek-Rapp q = qa + qb / k, per metre; a larger q leaves fewer "
       "large boulders. Also used for golombek_rapp ejecta.",
       "1/m");
    io("qb", boulders.qb,
       "Term of the Golombek-Rapp q = qa + qb / k divided by the coverage k, per metre.", "1/m");
    io("min_diameter", boulders.minDiameter,
       "Smallest boulder generated, in metres; lower values add many more small boulders.", "m");
    io("max_diameter", boulders.maxDiameter,
       "Largest boulder generated, in metres, exclusive; above the smallest.", "m");
    io("sizes", spec.sizes,
       "Size distribution of the boulders between craters, per m^2; patches and bedrock scale its "
       "density.");
    io("seed", boulders.seed,
       "Seed of the boulders, their attributes and the patches; changing it gives a different "
       "boulder "
       "field of the same statistics.");
    io("patches", boulders.patches,
       "Patches of richer and poorer boulder cover across the ground.");
    io("ejecta", boulders.ejecta,
       "Blocks thrown out by craters of the nearest crater layer before this one that dug into "
       "boulder, densest at the rim and thinning outward.");
    io("ejecta_sizes", spec.ejectaSizes,
       "Size distribution of the blocks at the rim of a fresh crater, per m^2; golombek_rapp uses "
       "the ejecta's coverage with qa and qb.");
    io("extrapolate", spec.extrapolate,
       "Lets the size distributions be evaluated outside their valid diameters, such as beyond a "
       "table's published ones; otherwise they must cover the boulder sizes.");
    io("exposure", boulders.exposure,
       "Raises the abundance of boulders where the ground beneath is bedrock, judged from its "
       "shape; "
       "unset disables it.");
    io("bedrock_abundance", boulders.bedrockAbundance,
       "Boulder abundance on bedrock over that on soil, and in between in proportion; used with "
       "the "
       "exposure field.");
    io("exposure_spacing", boulders.exposureSpacing,
       "Spacing in metres the ground is read at for the exposure field, fixed so that which "
       "boulders "
       "exist does not depend on the level of detail.",
       "m");
    io("min_height_ratio", boulders.minHeightRatio,
       "Smallest boulder height over diameter; each boulder's is drawn uniformly between the two "
       "ratios.");
    io("max_height_ratio", boulders.maxHeightRatio,
       "Largest boulder height over diameter; at least the smallest.");
    io("min_burial", boulders.minBurial,
       "Smallest buried share of a boulder's height; each boulder's is drawn uniformly between the "
       "two "
       "shares.");
    io("max_burial", boulders.maxBurial,
       "Largest buried share of a boulder's height, below 1; more burial leaves less of the "
       "boulder "
       "exposed.");
    io("max_lean", boulders.maxLean, "Largest tilt of a boulder from the vertical, in radians.",
       "rad");
    io("relief", boulders.relief, "How the boulders drawn into the heights look there.");
    io("models", boulders.models,
       "Which boulders become 3D models of their own rather than relief in the heights; neither "
       "threshold set means no models.");
    io("shapes", boulders.shapes,
       "The boulders' 3D boulder shapes, used by shape relief and by models.");
    io("shape_prototypes", boulders.shapePrototypes,
       "How many shapes the boulders share, so they can be drawn as instances; 0 gives every "
       "boulder a "
       "shape of its own.");
    io("fade", boulders.fade, "How boulders too small to resolve at the sample spacing fade out.");
}

template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec) {
    RoughnessOptions& roughness = spec.roughness;
    io("rms_slope", roughness.rmsSlope,
       "RMS slope, as rise over run, over the baseline when every octave is resolved; higher "
       "values give rougher ground.");
    io("baseline", roughness.baseline, "Baseline the RMS slope is given over, in metres.", "m");
    io("hurst", roughness.hurst,
       "Hurst exponent, in (0, 1]: octave amplitudes grow as wavelength^hurst, so lower values "
       "put relatively more roughness at short wavelengths.");
    io("max_wavelength", roughness.maxWavelength, "Wavelength of the longest octave, in metres.",
       "m");
    io("min_wavelength", roughness.minWavelength,
       "Wavelength of the shortest octave, in metres; octaves halve from the longest down to it, "
       "spanning at most 40 octaves.",
       "m");
    io("seed", roughness.seed,
       "Seed of the noise; changing it gives a different pattern of the same roughness.");
    io("fade", roughness.fade,
       "How octaves too fine to resolve at the sample spacing fade out, by samples per "
       "wavelength.");
}

template <typename IO> void Fields(IO& io, SecondaryLayerSpec& spec) {
    SecondaryCraterOptions& o = spec.secondaries;
    io("seed", o.seed,
       "Seed the secondaries are drawn with; changing it gives a different field of the same "
       "statistics.");
    io("onset_count", o.onsetCount,
       "Secondaries at least count_fraction primary diameters across, in an area the size of the "
       "primary at the onset range. Required: the sources give sizes, not numbers.");
    io("count_fraction", o.countFraction,
       "Size, as a fraction of the primary's diameter, from which the onset count counts "
       "secondaries.");
    io("size_slope", o.sizeSlope,
       "Cumulative size slope of the secondaries, their number falling as d^-slope; higher values "
       "give relatively more small secondaries.");
    io("min_diameter", o.minDiameter, "Smallest secondary generated, in metres.", "m");
    std::optional<double> maxDiameter;
    if (std::isfinite(o.maxDiameter)) {
        maxDiameter = o.maxDiameter;
    }
    io("max_diameter", maxDiameter,
       "Largest secondary generated, in metres; unset has no limit beyond Singer et al.'s largest "
       "secondary at each range.",
       "m");
    o.maxDiameter = maxDiameter.value_or(std::numeric_limits<double>::infinity());
    io("outer_range", o.outerRange,
       "How far secondaries reach, in primary diameters from its centre; unset uses the longest "
       "typical ray of Elliott et al. (2018).");
    io("min_primary_diameter", o.minPrimaryDiameter,
       "Smallest primary crater, in metres, that throws secondaries.", "m");
    std::optional<double> maxPrimary;
    if (std::isfinite(o.maxPrimaryDiameter)) {
        maxPrimary = o.maxPrimaryDiameter;
    }
    io("max_primary_diameter", maxPrimary,
       "Largest primary crater, in metres, that throws secondaries; unset has no upper limit.",
       "m");
    o.maxPrimaryDiameter = maxPrimary.value_or(std::numeric_limits<double>::infinity());
    io("min_age_gyr", o.minAgeGyr, "Youngest primary, in Gyr, that throws secondaries.", "Gyr");
    std::optional<double> maxAge;
    if (std::isfinite(o.maxAgeGyr)) {
        maxAge = o.maxAgeGyr;
    }
    io("max_age_gyr", maxAge,
       "Oldest primary, in Gyr, that throws secondaries; unset has no upper limit.", "Gyr");
    o.maxAgeGyr = maxAge.value_or(std::numeric_limits<double>::infinity());
    io("manual", o.manual,
       "Lets manual primary craters throw secondaries too; otherwise only generated ones do.");
    io("cell_size_factor", o.cellSizeFactor,
       "Side of the polar cells secondaries are drawn in, as a multiple of each size band's "
       "largest diameter.");
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter every secondary is given, with no wear by age; lower values give "
       "shallower secondaries.");
    io("body", spec.body, "Body whose fresh crater rim the secondaries are shaped with.");
}

template <typename IO> void Fields(IO& io, BedformLayerSpec& spec) {
    BedformOptions& bedforms = spec.bedforms;
    io("wavelength", bedforms.wavelength,
       "Crest-to-crest spacing, in metres; each field's varies from 0.8 to 1.2 times it.", "m");
    io("height", bedforms.height,
       "Trough-to-crest height, in metres, scaled with each field's wavelength.", "m");
    io("wind_azimuth", bedforms.windAzimuth,
       "Direction the wind blows toward, in radians counter-clockwise from +x (local east on a "
       "sphere); crests lie across it.",
       "rad");
    io("azimuth_spread", bedforms.azimuthSpread,
       "Largest difference of each field's wind from the azimuth, in radians.", "rad");
    io("lee_fraction", bedforms.leeFraction,
       "Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, lower values give "
       "a steeper lee face.");
    io("sinuosity", bedforms.sinuosity,
       "How far a crest wanders along its length, in wavelengths; 0 gives straight crests.");
    io("cover", bedforms.cover,
       "Share of the ground under bedform fields, from 0 to 0.9; 0 gives no bedforms.");
    io("field_size", bedforms.fieldSize, "Typical width of a bedform field, in metres.", "m");
    io("seed", bedforms.seed,
       "Seed of the fields and their crests; changing it gives a different layout of the same "
       "statistics.");
    io("exposure", bedforms.exposure,
       "Leaves out fields whose centres lie on bedrock, the likelier the barer the ground, as "
       "sand settles in lows; unset disables it.");
    io("exposure_spacing", bedforms.exposureSpacing,
       "Spacing in metres the ground is read at for the exposure field.", "m");
    io("fade", bedforms.fade,
       "How bedforms too fine for the sample spacing fade out, by samples per wavelength.");
}

template <typename IO> void Fields(IO& io, ScaleLayerSpec& spec) {
    io("scale", spec.scale,
       "Factor every height so far is multiplied by, as for vertical exaggeration.");
    io("offset", spec.offset, "Metres added to every height after scaling, to shift the datum.",
       "m");
}

template <typename IO> void Fields(IO& io, ClampLayerSpec& spec) {
    io("min", spec.min,
       "Lowest height kept, in metres; lower heights are raised to it, flattening basin floors. "
       "Unset leaves heights unbounded below.",
       "m");
    io("max", spec.max,
       "Highest height kept, in metres; higher heights are lowered to it, capping peaks. Unset "
       "leaves heights unbounded above.",
       "m");
}

template <typename IO> void Fields(IO& io, RectangleRegionSpec& spec) {
    io("min_x", spec.minX,
       "Lower edge along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("min_y", spec.minY,
       "Lower edge along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("max_x", spec.maxX,
       "Upper edge along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("max_y", spec.maxY,
       "Upper edge along y in the surface's coordinates; the latitude in degrees on a sphere.");
}

template <typename IO> void Fields(IO& io, CircleRegionSpec& spec) {
    io("x", spec.x,
       "Centre along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("y", spec.y,
       "Centre along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("radius", spec.radius, "Radius of the circle in metres, also on a sphere.", "m");
}

template <typename IO> void Fields(IO& io, RegionLayerSpec& spec) {
    io("shape", spec.shape, "Area the inner layer is applied in: a rectangle or a circle.");
    io("feather", spec.feather,
       "Distance in metres outside the shape over which the inner layer fades back to the "
       "unchanged height; 0 gives a hard edge.",
       "m");
    io("blend", spec.blend, "Curve of the fade across the feather.");
    io("layer", spec.layer,
       "Layer applied only inside the shape. A crater layer here does not count as the crater "
       "layer for later boulder ejecta or secondaries.");
}

template <typename IO> void Fields(IO& io, LunarSurfaceSpec& spec) {
    io("age_gyr", spec.ageGyr,
       "Age of the lunar surface in Gyr, from 0 to 4.6; older surfaces hold more craters, up to "
       "the Trask equilibrium. Also sets auto crater ages.",
       "Gyr");
}

template <typename IO> void Fields(IO& io, MarsSurfaceSpec& spec) {
    io("age_gyr", spec.ageGyr,
       "Age of the Martian surface in Gyr, above 0 and at most 4.6; older surfaces hold more "
       "craters. Also sets auto crater ages.",
       "Gyr");
}

template <typename IO> void Fields(IO& io, PowerLawSpec& spec) {
    io("coefficient", spec.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "metres; scales how many there are.");
    io("exponent", spec.exponent,
       "Exponent of the power law; larger values give relatively more small craters or boulders.");
}

template <typename IO> void Fields(IO& io, PiecewisePowerLawSpec& spec) {
    io("segments", spec.segments,
       "Power-law segments in strictly increasing order of start, each running to the next one's "
       "start; they must meet at their joins.");
}

template <typename IO> void Fields(IO& io, TabulatedSpec& spec) {
    io("diameters", spec.diameters,
       "Table diameters in diameter units, at least two, positive and strictly increasing; counts "
       "are interpolated linearly in log D and log N between them.");
    io("cumulative", spec.cumulative,
       "Cumulative count N(>=D) at each diameter, per area unit; positive and not increasing.");
    io("diameter_unit", spec.diameterUnit,
       "Metres per table diameter unit: 1 for metres, 1000 for km.", "m");
    io("area_unit", spec.areaUnit, "Square metres per table area unit: 1 for m^2, 1e6 for km^2.",
       "m^2");
}

template <typename IO> void Fields(IO& /*io*/, TraskSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, HartmannSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, MooreSpec& spec) {
    io("terrain", spec.terrain, "Terrain type whose crater distribution of Moore's to use.");
}

template <typename IO> void Fields(IO& /*io*/, ViperSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, AutoAgesSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, FreshAgesSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, UniformAgesSpec& spec) {
    io("lifetime_gyr", spec.lifetimeGyr,
       "Lifetime in Gyr, positive, that every crater's age is drawn uniformly within; with "
       "degradation, a crater this old is fully worn.",
       "Gyr");
}

template <typename IO> void Fields(IO& io, SteadyStateAgesSpec& spec) {
    io("age_gyr", spec.ageGyr,
       "Age of the surface in Gyr, from 0 to 4.6; it caps every crater's lifetime, so larger "
       "craters on an older surface can be older.",
       "Gyr");
    io("equilibrium", spec.equilibrium,
       "Equilibrium density that, with the production rate, sets how long small craters last "
       "before they are erased.");
    io("extended_production", spec.extendedProduction,
       "Extends the Neukum production function below its 10 m limit as a power law of slope "
       "-2.85, down to 2.5 m; off uses it only from 10 m.");
}

template <typename IO> void Fields(IO& io, FreshnessDegradationSpec& spec) {
    io("cutoff_depth_ratio", spec.cutoffDepthRatio,
       "Depth over diameter of a crater at the end of its lifetime, in (0, 0.1); lower values "
       "let old craters wear shallower.");
}

template <typename IO> void Fields(IO& io, DiffusionDegradationSpec& spec) {
    io("diffusivity", spec.diffusivity,
       "Model of the diffusion age each crater has from its diameter and age; on Mars only "
       "power_law is allowed.");
    io("power_law", spec.powerLaw, "The diffusivity used when the diffusivity model is power_law.");
    io("reach_radii", spec.reachRadii,
       "Distance from the centre, in crater radii, out to which the diffused profile is solved "
       "and held at 0; at least the fresh profile's reach.");
}

template <typename IO> void Fields(IO& io, PowerLawDiffusivity& law) {
    io("coefficient", law.coefficient,
       "Diffusivity of a 1 km crater, in m^2/Myr, positive; higher values wear craters down "
       "faster.",
       "m^2/Myr");
    io("exponent", law.exponent,
       "Exponent of the diameter in coefficient (D / 1 km)^exponent; 0 gives the same "
       "diffusivity for every crater size.");
    io("min_diameter", law.minDiameter,
       "Diameter in metres below which the diffusivity is held at its value at this "
       "diameter; not negative.",
       "m");
}

template <typename IO> void Fields(IO& io, NoDegradationSpec& spec) {
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter given to every crater, positive; unset keeps each crater's fresh "
       "depth for the body.");
}

template <typename IO> void Fields(IO& io, FassettThomsonSpec& spec) {
    io("rim", spec.rim, "How the profile's interior and exterior fits are joined at the rim.");
}

template <typename IO> void Fields(IO& /*io*/, MartinParkesDunstanSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, ComplexProfileSpec& spec) {
    io("simple", spec.simple,
       "Profile of the simple craters, those below the body's simple-to-complex transition "
       "diameter.");
}

template <typename IO> void Fields(IO& io, BoulderListOutput& output) {
    io("path", output.path,
       "File to write; relative paths resolve against the output folder (the CLI's "
       "--output-dir), else the configuration's folder.");
    io("format", output.format,
       "Format of the file; when not given, .json paths are written as JSON and others as "
       "CSV.");
}

template <typename IO> void Fields(IO& io, BoulderModelsOutput& output) {
    io("path", output.path,
       "File to write, ending in .glb or .ply; relative paths resolve against the output folder "
       "(the CLI's --output-dir), else the configuration's folder.");
    io("crease_angle", output.creaseAngle,
       "Angle in degrees, from 0 to 180: in .glb files, vertices are split where faces meet at "
       "more than it, giving sharp edges; .ply files ignore it.",
       "deg");
}

template <typename IO> void Fields(IO& io, BoulderModelThreshold& models) {
    io("min_diameter", models.minDiameter,
       "Boulders at least this many metres across become 3D models; unset applies no diameter "
       "threshold.",
       "m");
    io("min_samples", models.minSamples,
       "Boulders spanning at least this many samples at the spacing evaluated become 3D models, so "
       "finer levels of detail make more of them; unset applies no such threshold.");
}

template <typename IO> void Fields(IO& io, ValueRange& range) {
    io("min", range.min, "Lower end of the interval the value is drawn uniformly from.");
    io("max", range.max, "Upper end of the interval the value is drawn uniformly from.");
}

template <typename IO> void Fields(IO& io, BoulderStyle& style) {
    io("name", style.name, "Label of the style, for reference; it does not change the shapes.");
    io("weight", style.weight,
       "Relative share of boulders given this style, not negative; the styles' weights must not "
       "all be 0.");
    io("solid", style.solid,
       "Solid the shape starts from, before it is stretched, rounded, roughened and fractured.");
    io("intermediate", style.intermediate,
       "Range of the intermediate axis over the long one, b / a, within (0, 1]; lower values "
       "give more elongated boulders.");
    io("short_axis", style.shortAxis,
       "Range of the short, vertical axis over the long one, c / a, within (0, 1] and at most "
       "the intermediate ratio; lower values give flatter boulders.");
    io("rounding", style.rounding,
       "Range of the share, from 0 to 1, by which the shape is rounded toward the ellipsoid of "
       "its axes: 0 keeps the solid's edges and corners, 1 is the ellipsoid.");
    io("min_fractures", style.minFractures,
       "Fewest flat fracture planes cutting a boulder, from 0; each boulder's count is drawn "
       "between the two.");
    io("max_fractures", style.maxFractures,
       "Most flat fracture planes cutting a boulder, at most 64 and at least the fewest.");
    io("fracture_depth", style.fractureDepth,
       "Range of where each fracture plane lies, as a share of the shape's reach along its "
       "normal, within (0, 1]; lower values cut more away.");
    io("max_fracture_tilt", style.maxFractureTilt,
       "Largest vertical component of a fracture plane's unit normal, from 0 to 1: 0 keeps "
       "normals horizontal, cutting a slab about its rim only.");
    io("noise_amplitude", style.noiseAmplitude,
       "Largest relative change of the radius by the roughening noise, in [0, 1); 0 leaves the "
       "surface unroughened.");
    io("noise_wavelength", style.noiseWavelength,
       "Wavelength of the roughening noise's first octave, in long-axis lengths; positive, "
       "with smaller values giving finer bumps.");
    io("noise_octaves", style.noiseOctaves,
       "Octaves of the roughening noise, 1 to 12, each halving in wavelength; more add finer "
       "detail.");
}

template <typename IO> void Fields(IO& io, BoulderShapeOptions& shapes) {
    io("styles", shapes.styles,
       "Kinds of boulder each shape is drawn from, picked by weight; at least one.");
    io("subdivisions", shapes.subdivisions,
       "Times the solid's triangles are split in four, 0 to 6; more give finer, smoother shapes "
       "with more triangles.");
    io("seed", shapes.seed,
       "Seed the shapes are drawn with, along with each shape's own seed; changing it reshapes "
       "every boulder.");
}

template <typename IO> void Fields(IO& io, CraterListOutput& output) {
    io("path", output.path,
       "File to write; relative paths resolve against the output folder (the CLI's "
       "--output-dir), else the configuration's folder.");
    io("format", output.format,
       "Format of the file; when not given, .json paths are written as JSON and others as "
       "CSV.");
}

template <typename IO> void Fields(IO& /*io*/, PlaneSurfaceSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, SphereSurfaceSpec& spec) {
    io("radius", spec.radius,
       "Radius of the body's datum sphere, in metres; give it or a body, not both.", "m");
    io("body", spec.body,
       "Body whose mean radius to use instead of a radius; unset requires the radius.");
}

template <typename IO> void Fields(IO& io, BodyMeshOutput& output) {
    io("path", output.path,
       "File to write, ending in .glb or .ply; relative paths resolve against the output folder "
       "(the CLI's --output-dir), else the configuration's folder.");
    io("cells_per_face", output.cellsPerFace,
       "Quads along each edge of the six cube faces, 1 to 4096; more give a finer mesh, with two "
       "triangles per quad.");
    io("spacing", output.spacing,
       "Sample spacing in metres the heights are evaluated at, which sets the finest features "
       "kept; unset uses a quad's side at a face's centre.",
       "m");
}

template <typename IO> void Fields(IO& io, AlbedoOutput& output) {
    io("path", output.path,
       "GeoTIFF file to write; relative paths resolve against the output folder (the CLI's "
       "--output-dir), else the configuration's folder.");
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size", output.tileSize,
       "Width and height of the file's tiles in pixels; a positive multiple of 16.", "px");
}

template <typename IO> void Fields(IO& io, SyntheticAlbedoSpec& spec) {
    io("rgb", spec.rgb,
       "Gives three bands, red, green and blue, from the colour; off gives one band of the value "
       "alone, ignoring hue and saturation.");
    io("hue", spec.hue, "Hue of the base colour in degrees, wrapping around; used with rgb only.",
       "deg");
    io("saturation", spec.saturation,
       "Saturation of the base colour, from 0 for grey to 1; used with rgb only.");
    io("value", spec.value,
       "Reflectance of the brightest band, from 0 to 1; the one band's value without rgb. "
       "Higher values give a brighter surface.");
    io("hue_noise", spec.hueNoise,
       "Noise layers, 0 to 8, whose sum in degrees is added to the hue; used with rgb only.");
    io("saturation_noise", spec.saturationNoise,
       "Noise layers, 0 to 8, added to the saturation, which is then clamped to [0, 1]; used "
       "with rgb only.");
    io("value_noise", spec.valueNoise,
       "Noise layers, 0 to 8: the value is multiplied by 1 plus their sum and clamped to [0, 1], "
       "so their amplitude sets the relative contrast.");
}

template <typename IO> void Fields(IO& io, AlbedoDetail& detail) {
    io("enabled", detail.enabled,
       "Adds fractal detail below the map's resolution; off leaves the interpolated map as it "
       "is.");
    io("seed", detail.seed,
       "Seed of the detail noise; changing it gives a different pattern of the same roughness.");
    io("octaves", detail.octaves,
       "Octaves of detail, 1 to 16, from half the map's cell size down, each half the wavelength "
       "of the one before.");
    io("scale", detail.scale,
       "Factor on the RMS of every octave, not negative; 0 adds no detail, higher values give a "
       "more mottled albedo.");
    io("hurst", detail.hurst,
       "Hurst exponent, in [0, 1], used instead of the one measured from the map; lower values "
       "keep more contrast at fine scales. Unset uses the measured one.");
    io("fade", detail.fade, "How octaves of detail too fine for the sample spacing fade out.");
}

template <typename IO> void Fields(IO& io, RasterAlbedoSpec& spec) {
    io("path", spec.path,
       "GeoTIFF albedo map in the terrain's projected coordinates; relative paths resolve "
       "against the configuration's folder.");
    io("bands", spec.bands,
       "Bands of the file to read, counting from 0: one for a single albedo band, or three for "
       "red, green and blue.");
    io("scale", spec.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata, as for maps stored as integers. Unset keeps the file's.");
    io("offset", spec.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.");
    io("resampling", spec.options.method,
       "Interpolation used to sample the map at the terrain's points.");
    io("detail", spec.options.detail,
       "Fractal detail below the map's resolution, continuing the map's own measured roughness "
       "to finer scales.");
}

template <typename IO> void Fields(IO& io, CraterRays& rays) {
    io("enabled", rays.enabled,
       "Gives craters rays of bright material; off leaves only their interiors and ejecta "
       "bright.");
    io("count", rays.count,
       "Number of rays of a crater, drawn per crater and rounded; from 0 to 64.");
    io("length_coefficient", rays.lengthCoefficient,
       "Coefficient of the median ray length, coefficient R^exponent km for crater radius R in "
       "km; higher values give longer rays.");
    io("length_exponent", rays.lengthExponent,
       "Exponent of the crater radius in the median ray length, positive; higher values make "
       "large craters' rays relatively longer.");
    io("length_spread", rays.lengthSpread,
       "Natural logarithm of a ray's length over the median, drawn per ray; a wider "
       "distribution gives more varied lengths.");
    io("width_degrees", rays.widthDegrees,
       "Angle in degrees a ray spans 2 radii from the centre, drawn per ray, within (0, 180); "
       "beyond 2 radii a ray keeps the width it has there.");
    io("excess", rays.excess,
       "Optical maturity above the background along a ray at the rim, falling linearly to 0 at "
       "its end; not negative, higher values give brighter rays.");
    io("patchiness", rays.patchiness,
       "Share of a ray's strength, from 0 to 1, that noise along it can take away, breaking it "
       "into patches; 0 gives unbroken rays.");
    io("patch_radii", rays.patchRadii,
       "Wavelength of the noise along a ray, in crater radii; positive, smaller values give "
       "shorter patches.");
    io("oblique", rays.oblique,
       "How oblique impacts lengthen rays downrange and clear them uprange.");
}

template <typename IO> void Fields(IO& io, ObliqueImpacts& oblique) {
    io("enabled", oblique.enabled,
       "Draws an impact angle and direction per crater that shape its rays; off treats every "
       "impact as vertical.");
    io("heading", oblique.heading,
       "Downrange direction of every impact, in radians counter-clockwise from east; unset draws "
       "a random heading per crater.",
       "rad");
    io("asymmetric_degrees", oblique.asymmetricDegrees,
       "Impact angle from the horizontal, in degrees, below which ejecta leave faster downrange, "
       "lengthening rays there and shortening them uprange.",
       "deg");
    io("forbidden_degrees", oblique.forbiddenDegrees,
       "Impact angle in degrees below which rays within the zone half width of uprange are "
       "dropped; at most the asymmetric angle.",
       "deg");
    io("butterfly_degrees", oblique.butterflyDegrees,
       "Impact angle in degrees below which rays within the zone half width of downrange are "
       "dropped too, a butterfly pattern; at most the forbidden angle.",
       "deg");
    io("downrange_speed_ratio", oblique.downrangeSpeedRatio,
       "Downrange over uprange ejection speed below the asymmetric angle, drawn per crater; at "
       "least 1, higher values stretch downrange rays more.");
    io("zone_half_width_degrees", oblique.zoneHalfWidthDegrees,
       "Half width in degrees, 0 to 180, of the ray-free zones uprange and downrange; "
       "illustrative, as the sources do not measure it.",
       "deg");
}

template <typename IO> void Fields(IO& io, RayRetention& retention) {
    io("small_diameter", retention.smallDiameter,
       "Diameter in metres at or below which craters keep their bright material for the small "
       "time.",
       "m");
    io("small_myr", retention.smallMyr,
       "Time in Myr craters up to the small diameter keep their bright material.", "Myr");
    io("mid_diameter", retention.midDiameter,
       "Diameter in metres from which craters keep their bright material for the mid time; "
       "between the small and mid diameters the time follows a power law of diameter.",
       "m");
    io("mid_myr", retention.midMyr,
       "Time in Myr craters from the mid to the large diameter keep their bright material.", "Myr");
    io("large_diameter", retention.largeDiameter,
       "Diameter in metres above which craters keep their bright material for the large time.",
       "m");
    io("large_myr", retention.largeMyr,
       "Time in Myr craters above the large diameter keep their bright material.", "Myr");
    io("scale", retention.scale,
       "Factor every retention time is multiplied by, positive; higher values keep craters "
       "bright for longer.");
}

template <typename IO> void Fields(IO& io, CraterAlbedoOptions& options) {
    io("seed", options.seed,
       "Seed of which craters get bright material, their rays and their impacts; changing it "
       "gives a different pattern of the same statistics.");
    io("min_diameter", options.minDiameter,
       "Smallest diameter, in metres, of craters given bright material.", "m");
    std::optional<double> maxDiameter;
    if (std::isfinite(options.maxDiameter)) {
        maxDiameter = options.maxDiameter;
    }
    io("max_diameter", maxDiameter,
       "Largest diameter, in metres, of craters given bright material; unset has no upper "
       "limit.",
       "m");
    options.maxDiameter = maxDiameter.value_or(std::numeric_limits<double>::infinity());
    io("min_age_gyr", options.minAgeGyr, "Youngest age, in Gyr, of craters given bright material.",
       "Gyr");
    std::optional<double> maxAge;
    if (std::isfinite(options.maxAgeGyr)) {
        maxAge = options.maxAgeGyr;
    }
    io("max_age_gyr", maxAge,
       "Oldest age, in Gyr, of craters given bright material; unset has no upper limit.", "Gyr");
    options.maxAgeGyr = maxAge.value_or(std::numeric_limits<double>::infinity());
    io("manual", options.manual,
       "Gives manual craters bright material too; otherwise only generated craters have it.");
    io("share", options.share, "Share of eligible craters, from 0 to 1, given bright material.");
    io("background_maturity", options.backgroundMaturity,
       "Optical maturity (OMAT) of the ground around the craters, positive; the excesses are "
       "added to it and the albedo is brightened from it.");
    io("max_maturity", options.maxMaturity,
       "Cap on the optical maturity where craters' excesses add up, at least the background; it "
       "limits how bright overlapping craters get.");
    io("interior_excess", options.interiorExcess,
       "Optical maturity above the background inside a fresh crater's rim, not negative; higher "
       "values give brighter interiors.");
    io("ejecta_excess", options.ejectaExcess,
       "Optical maturity above the background on a fresh crater's continuous ejecta at the rim, "
       "not negative, falling with distance.");
    io("ejecta_exponent", options.ejectaExponent,
       "Exponent of the ejecta's fall with distance r, as (r / R)^-exponent for crater radius R; "
       "positive, higher values keep the bright ejecta nearer the rim.");
    io("ejecta_reach_radii", options.ejectaReachRadii,
       "Distance from the centre, in crater radii and over 1, at which the ejecta's excess "
       "reaches 0.");
    io("rays", options.rays, "Rays of bright material running out from the craters.");
    io("retention", options.retention,
       "How long craters of each diameter keep their bright material.");
    io("fade_exponent", options.fadeExponent,
       "Exponent of the fade with age t, max(0, 1 - t / T)^exponent for retention time T; "
       "positive, 1 is a straight fade and higher values fade sooner.");
    io("origin_reflectance", options.originReflectance,
       "Reflectance of the optical maturity origin, the hypermature end-member, from which "
       "reflectance scales with maturity at fixed iron content.");
    io("fade", options.fade,
       "How bright material of craters too small to resolve at the sample spacing fades out.");
}

template <typename IO> void Fields(IO& io, CraterAlbedoSpec& spec) {
    io("crater_layer", spec.craterLayer,
       "Which top-level crater layer's craters to brighten, counting from 0; unset uses every "
       "top-level crater layer.");
    Fields(io, spec.options);
}

template <typename IO> void Fields(IO& io, AlbedoSpec& spec) {
    io("source", spec.source,
       "Base albedo of the terrain, before its layers: a synthetic colour or a raster map.");
    io("layers", spec.layers,
       "Layers applied to the source's albedo in order, such as bright crater ejecta and rays.");
}

TerrainConfig ReadConfig(const Json& json) {
    Reader reader(json, "");
    if (!reader.Has("schema_version")) {
        Fail("schema_version",
             "required; this library reads version " + std::to_string(kConfigSchemaVersion));
    }
    TerrainConfig config;
    reader("schema_version", config.schemaVersion);
    if (config.schemaVersion != kConfigSchemaVersion) {
        Fail("schema_version", "version " + std::to_string(config.schemaVersion) +
                                   " is not supported; this library reads version " +
                                   std::to_string(kConfigSchemaVersion));
    }
    reader("surface", config.surface);
    reader("source", config.source);
    reader("layers", config.layers);
    reader("filters", config.filters);
    reader("outputs", config.outputs);
    reader("albedo", config.albedo);
    reader.Finish();
    return config;
}

}  // namespace

ConfigError::ConfigError(std::string path, const std::string& message)
    : std::runtime_error(path.empty() ? message : path + ": " + message), path_(std::move(path)) {}

TerrainConfig ParseConfig(std::string_view json) {
    Json document;
    try {
        document = Json::parse(json);
    } catch (const Json::exception& error) {
        throw ConfigError("", std::string("invalid JSON: ") + error.what());
    }
    return ReadConfig(document);
}

TerrainConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ConfigError("", "cannot read " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseConfig(text.str());
}

std::string ConfigToJson(const TerrainConfig& config, int indent) {
    Json json = Json::object();
    json["schema_version"] = config.schemaVersion;
    json["surface"] = ToJson(config.surface);
    json["source"] = ToJson(config.source);
    json["layers"] = ToJson(config.layers);
    json["filters"] = ToJson(config.filters);
    json["outputs"] = ToJson(config.outputs);
    if (config.albedo) {
        json["albedo"] = ToJson(*config.albedo);
    }
    return json.dump(indent) + "\n";
}

namespace {

/// Every alternative of `Variant`, default-constructed, with its "type".
template <typename... Ts> Json Alternatives(const std::variant<Ts...>* /*tag*/) {
    Json json = Json::array();
    (json.push_back(ToJson(std::variant<Ts...>(Ts{}))), ...);
    return json;
}

template <typename Variant> Json Alternatives() {
    return Alternatives(static_cast<const Variant*>(nullptr));
}

/// What `ConfigSchema` collects: each struct and variant described once under an id, so
/// recursive types (a chain of sources) refer to themselves.
struct SchemaBuilder {
    Json definitions = Json::object();
    std::map<std::type_index, std::string> ids;

    /// A reference to `T`'s definition, made by `describe` the first time.
    template <typename T, typename Describe> Json Reference(Describe describe) {
        const auto found = ids.find(typeid(T));
        if (found != ids.end()) {
            return Json{{"kind", "ref"}, {"ref", found->second}};
        }
        const std::string id = "d" + std::to_string(ids.size());
        ids.emplace(typeid(T), id);
        definitions[id] = describe();
        return Json{{"kind", "ref"}, {"ref", id}};
    }
};

template <typename T> struct Describer;

/// Describes each field a `Fields` function visits: its name, schema and default.
class SchemaWriter {
  public:
    SchemaWriter(SchemaBuilder& builder, Json& fields) : builder_(builder), fields_(fields) {}

    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {}) {
        fields_.push_back(Json{{"name", std::string(key)},
                               {"description", std::string(description)},
                               {"unit", std::string(unit)},
                               {"schema", Describer<T>::Describe(builder_)},
                               {"default", ToJson(value)}});
    }

  private:
    SchemaBuilder& builder_;
    Json& fields_;
};

template <> struct Describer<double> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "number"}};
    }
};

template <> struct Describer<float> : Describer<double> {};

template <> struct Describer<int> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "integer"}};
    }
};

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
struct Describer<T> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "integer"}, {"minimum", 0}};
    }
};

template <> struct Describer<bool> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "boolean"}};
    }
};

template <> struct Describer<std::string> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "string"}};
    }
};

template <NamedEnum E> struct Describer<E> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        Json choices = Json::array();
        Json descriptions = Json::array();
        for (const auto& entry : EnumNames<E>::kValues) {
            choices.push_back(std::string(EntryName(entry)));
            descriptions.push_back(std::string(EntryDescription(entry)));
        }
        return Json{{"kind", "enum"}, {"choices", choices}, {"descriptions", descriptions}};
    }
};

template <typename T> struct Describer<std::optional<T>> {
    static Json Describe(SchemaBuilder& builder) {
        return Json{{"kind", "optional"},
                    {"of", Describer<T>::Describe(builder)},
                    {"default", ToJson(T{})}};
    }
};

template <typename T> struct Describer<Boxed<T>> : Describer<T> {};

template <typename T> struct Describer<std::vector<T>> {
    static Json Describe(SchemaBuilder& builder) {
        return Json{
            {"kind", "array"}, {"of", Describer<T>::Describe(builder)}, {"default", ToJson(T{})}};
    }
};

template <typename... Ts> struct Describer<std::variant<Ts...>> {
    static Json Describe(SchemaBuilder& builder) {
        return builder.Reference<std::variant<Ts...>>([&] {
            Json alternatives = Json::array();
            const auto describe = [&]<typename T>(T* /*tag*/) {
                Json fields = Json::array();
                T spec{};
                SchemaWriter writer(builder, fields);
                Fields(writer, spec);
                std::string description;
                if constexpr (requires { SpecName<T>::kDescription; }) {
                    description = SpecName<T>::kDescription;
                }
                alternatives.push_back(Json{{"type", std::string(SpecName<T>::kName)},
                                            {"description", description},
                                            {"fields", fields},
                                            {"default", ToJson(std::variant<Ts...>(T{}))}});
            };
            (describe(static_cast<Ts*>(nullptr)), ...);
            return Json{{"kind", "choice"}, {"alternatives", alternatives}};
        });
    }
};

template <HasFields T> struct Describer<T> {
    static Json Describe(SchemaBuilder& builder) {
        return builder.Reference<T>([&] {
            Json fields = Json::array();
            T value{};
            SchemaWriter writer(builder, fields);
            Fields(writer, value);
            return Json{{"kind", "object"}, {"fields", fields}};
        });
    }
};

/// A field of the configuration's root object, described as `SchemaWriter` does.
template <typename T>
Json RootField(SchemaBuilder& builder, std::string_view name, const T& value,
               std::string_view description) {
    return Json{{"name", std::string(name)},
                {"description", std::string(description)},
                {"schema", Describer<T>::Describe(builder)},
                {"default", ToJson(value)}};
}

}  // namespace

std::string ConfigCatalog(int indent) {
    Json json = Json::object();
    json["schema_version"] = kConfigSchemaVersion;
    json["surface"] = Alternatives<SurfaceSpec>();
    json["source"] = Alternatives<SourceSpec>();
    json["layers"] = Alternatives<LayerSpec>();
    json["filters"] = Alternatives<FilterSpec>();
    json["outputs"] = Alternatives<OutputSpec>();
    json["albedo"] = ToJson(AlbedoSpec{});
    Json models = Json::object();
    models["noise_layer"] = ToJson(NoiseLayerConfig{});
    models["density"] = Alternatives<DensitySpec>();
    models["ages"] = Alternatives<AgesSpec>();
    models["degradation"] = Alternatives<DegradationSpec>();
    models["profile"] = Alternatives<ProfileSpec>();
    models["boulder_sizes"] = Alternatives<BoulderSizesSpec>();
    models["region_shape"] = Alternatives<RegionShapeSpec>();
    models["albedo_source"] = Alternatives<AlbedoSourceSpec>();
    models["albedo_layer"] = Alternatives<AlbedoLayerSpec>();
    json["models"] = models;
    return json.dump(indent) + "\n";
}

std::string ConfigSchema(int indent) {
    SchemaBuilder builder;
    const TerrainConfig config;
    Json fields = Json::array();
    fields.push_back(
        Json{{"name", "schema_version"},
             {"description", "Version of the configuration format, which this library reads; "
                             "required."},
             {"schema", Json{{"kind", "integer"}, {"fixed", true}}},
             {"default", config.schemaVersion}});
    fields.push_back(RootField(builder, "surface", config.surface,
                               "What the terrain lies on: a plane in metres, or a sphere (a "
                               "planet or moon) in longitude and latitude degrees."));
    fields.push_back(RootField(builder, "source", config.source,
                               "The starting heights and the grid every layer, filter and "
                               "output uses: flat, read from a GeoTIFF, or a chain of sources."));
    fields.push_back(RootField(builder, "layers", config.layers,
                               "Relief and features added to the source's heights, applied in "
                               "order: noise, craters, boulders and more."));
    fields.push_back(RootField(builder, "filters", config.filters,
                               "Processing applied to the heights after every layer."));
    fields.push_back(RootField(builder, "outputs", config.outputs,
                               "Files written after generation: heightmaps, crater and boulder "
                               "lists, boulder models, albedo and body meshes."));
    fields.push_back(RootField(builder, "albedo", config.albedo,
                               "The terrain's albedo: a source and the layers that change it, "
                               "over the terrain's grid; none when unset."));
    Json json = Json::object();
    json["schema_version"] = kConfigSchemaVersion;
    json["root"] = Json{{"kind", "object"}, {"fields", fields}};
    json["definitions"] = builder.definitions;
    return json.dump(indent) + "\n";
}

}  // namespace terraingen
