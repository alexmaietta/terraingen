/// @file
/// @brief The JSON name and description of every enum value a configuration holds.
#pragma once

#include "terraingen/pipeline/config.h"

#include <array>
#include <string_view>
#include <type_traits>

namespace terraingen::config {

/// @brief One named value of an enum in the configuration, and what it means, for editors.
template <typename E> struct EnumEntry {
    std::string_view name;           ///< The JSON name.
    E value;                         ///< The value.
    std::string_view description{};  ///< What it means, for editors' tooltips.
};

/// @brief Every value of the enum `E` as an `EnumEntry` (`kValues`), specialized per enum below.
template <typename E> struct EnumNames;

/// @brief An enum with `EnumNames`, read and written by name.
template <typename E>
concept NamedEnum = std::is_enum_v<E> && requires { EnumNames<E>::kValues; };

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

template <> struct EnumNames<ProductionFunction> {
    static constexpr std::array<EnumEntry<ProductionFunction>, 3> kValues{{
        {"neukum", ProductionFunction::Neukum,
         "Neukum et al. (2001) lunar production function, valid from 10 m to 300 km."},
        {"extended_neukum", ProductionFunction::ExtendedNeukum,
         "The Neukum production function continued below 10 m as a power law of slope -2.85, "
         "valid from 2.5 m to 300 km."},
        {"ivanov", ProductionFunction::Ivanov,
         "Ivanov (2001) Martian production function, valid from 15 m to 362 km."},
    }};
};

template <> struct EnumNames<ChronologyModel> {
    static constexpr std::array<EnumEntry<ChronologyModel>, 2> kValues{{
        {"neukum", ChronologyModel::Neukum,
         "Neukum et al. (2001) lunar chronology, for surfaces 0 to 4.6 Gyr old."},
        {"ivanov", ChronologyModel::Ivanov,
         "Ivanov (2001) Martian chronology, for surfaces 0 to 4.6 Gyr old."},
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

template <> struct EnumNames<CraterMorphometryFit> {
    static constexpr std::array<EnumEntry<CraterMorphometryFit>, 2> kValues{{
        {"lunar", CraterMorphometryFit::Lunar,
         "Fresh lunar craters: Fassett et al. (2022) depth and rim height below 15 km, Pike's "
         "(1977) complex crater fits above."},
        {"mars", CraterMorphometryFit::Mars,
         "Fresh Martian craters: depth and rim height capped for small craters, Garvin et al.'s "
         "(2003) fits for large and complex craters from 7 km."},
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
         "its own."},
    }};
};

template <> struct EnumNames<ListFormat> {
    static constexpr std::array<EnumEntry<ListFormat>, 2> kValues{{
        {"csv", ListFormat::Csv, "Comma-separated values, one row per item."},
        {"json", ListFormat::Json, "A JSON document."},
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

}  // namespace terraingen::config
