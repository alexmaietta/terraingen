// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of fades, exposure, density maps, roughness and spectral continuation,
/// and the names of their enums.
#pragma once

#include "terraingen/layers/density_map.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/util/option_fields.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace terraingen {

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

template <> struct EnumNames<DensityMeasure> {
    static constexpr std::array<EnumEntry<DensityMeasure>, 3> kValues{{
        {"scale", DensityMeasure::Scale,
         "The local density over the layer's own; 1 keeps it as the size distribution gives it."},
        {"count", DensityMeasure::Count,
         "Features with diameters in the map's range per area unit, such as boulders per km^2."},
        {"areal_fraction", DensityMeasure::ArealFraction,
         "Share of the ground covered by features in the map's range, such as rock abundance."},
    }};
};

template <> struct EnumNames<SizeSlopeMeasure> {
    static constexpr std::array<EnumEntry<SizeSlopeMeasure>, 2> kValues{{
        {"exponent", SizeSlopeMeasure::Exponent,
         "The cumulative exponent alpha, counts falling as D^-alpha."},
        {"mean_diameter", SizeSlopeMeasure::MeanDiameter,
         "The mean diameter in meters of the features from the reference diameter up, from which "
         "alpha = mean / (mean - D0)."},
    }};
};

template <typename IO> void Fields(IO& io, ResolutionFade& fade) {
    io("start_samples", fade.startSamples,
       "Samples across a feature's diameter up to which it is left out entirely.", "",
       AtLeast(0.0));
    io("end_samples", fade.endSamples,
       "Samples across a feature's diameter from which it is applied in full; it fades in between "
       "the two counts.",
       "", Above(0.0));
    io("shape", fade.shape, "Curve of the fade between the start and end sample counts.");
}

template <typename IO> void Fields(IO& io, ExposureParams& exposure) {
    io("reach_m", exposure.reach,
       "Distance in meters around a point at which its surroundings are read, to tell rises from "
       "lows.",
       "m", Above(0.0));
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
       "Share of level ground that is bedrock too, in noise patches, as on a swept lava floor; 0 "
       "for none.",
       "", FromBelow(0.0, 1.0));
    io("floor_patch_m", exposure.floorPatch,
       "Width of the bedrock patches on level ground, in meters.", "m", Above(0.0));
    io("seed", exposure.seed, "Seed of the bedrock patches on level ground.");
}

/// The fields of a density map but its field, which is given apart.
template <typename IO> void Fields(IO& io, DensityMap& map) {
    io("measure", map.measure, "What the map's values measure.");
    io("min_diameter_m", map.minDiameter,
       "Smallest diameter the map measures, in meters; positive for a count or areal fraction.",
       "m", AtLeast(0.0));
    OptionalBound(io, "max_diameter_m", map.maxDiameter,
                  "Largest diameter the map measures, in meters, exclusive; unset for no limit.",
                  "m", std::numeric_limits<double>::infinity(), Above(0.0));
    io("area_unit", map.areaUnit,
       "Square meters per area unit of a count: 1 for per m^2, 1e6 for per km^2.", "m^2",
       Above(0.0));
    io("total", map.total,
       "The map counts every feature of its kind, including the secondaries thrown from a crater "
       "layer's craters and the boulders a layer throws out around craters, which are taken out "
       "of it first; off when it counts only what the layer generates.");
}

/// The fields of a size slope map but its field, which is given apart.
template <typename IO> void Fields(IO& io, SizeSlopeMap& map) {
    io("measure", map.measure, "What the map's values measure.");
    io("reference_diameter_m", map.referenceDiameter,
       "Diameter D0 in meters where the local power law meets the layer's own count, as N(>=D) = "
       "N(>=D0) (D / D0)^-alpha; for a mean diameter map, the smallest diameter averaged.",
       "m", Above(0.0));
    io("min_exponent", map.minExponent,
       "Smallest exponent alpha the map's values are clamped to; with the largest it bounds the "
       "boulders drawn, so generous bounds draw more to thin.",
       "", Above(0.0));
    io("max_exponent", map.maxExponent,
       "Largest exponent alpha the map's values are clamped to; above the smallest.", "",
       Above(0.0));
}

/// What a roughness's "rms_slope" field means.
inline constexpr std::string_view kRmsSlopeHelp =
    "RMS slope, as rise over run, over the baseline when every octave is resolved; higher values "
    "give rougher ground.";

/// What a roughness's "hurst" field means.
inline constexpr std::string_view kRoughnessHurstHelp =
    "Hurst exponent: octave amplitudes grow as wavelength^hurst, so lower values put relatively "
    "more roughness at short wavelengths.";

/// The fields of a roughness's octaves: their wavelengths, seed and fade.
template <typename IO> void RoughnessOctaveFields(IO& io, RoughnessOptions& roughness) {
    io("max_wavelength_m", roughness.maxWavelength, "Wavelength of the longest octave, in meters.",
       "m", Above(0.0));
    io("min_wavelength_m", roughness.minWavelength,
       "Wavelength of the shortest octave, in meters; octaves halve from the longest down to it, "
       "spanning at most 40 octaves.",
       "m", Above(0.0));
    io("seed", roughness.seed,
       "Seed of the noise; changing it gives a different pattern of the same roughness.");
    FadePixelFields(
        io, roughness.fade,
        "How octaves too fine to resolve at the sample spacing fade out, by samples per "
        "wavelength.");
}

template <typename IO> void Fields(IO& io, RoughnessOptions& roughness) {
    io("rms_slope", roughness.rmsSlope, std::string(kRmsSlopeHelp), "", Above(0.0));
    io("baseline_m", roughness.baseline, "Baseline the RMS slope is given over, in meters.", "m",
       Above(0.0));
    io("hurst", roughness.hurst, std::string(kRoughnessHurstHelp), "", AboveUpTo(0.0, 1.0));
    RoughnessOctaveFields(io, roughness);
}

/// The fields a spectral continuation shares between its options and its layer spec.
template <typename IO>
void SpectralSynthesisFields(IO& io, double& minWavelength, double& taperOctaves,
                             std::uint32_t& seed, ResolutionFade& fade) {
    io("min_wavelength_m", minWavelength,
       "Shortest wavelength of the detail in meters; the detail spans the wavelengths from twice "
       "the source's pixel size down to it.",
       "m", Above(0.0));
    io("taper_octaves", taperOctaves,
       "Octaves above the source's Nyquist frequency over which the detail fades in, so it adds "
       "nothing at wavelengths the source resolves.",
       "", Above(0.0));
    io("seed", seed,
       "Seed of the detail; changing it gives a different pattern of the same "
       "spectrum.");
    FadePixelFields(
        io, fade,
        "How wavelengths too fine for the sample spacing fade out, by samples per wavelength.");
}

template <typename IO> void Fields(IO& io, SpectralContinuationOptions& options) {
    io("pixel_size_m", options.pixelSize,
       "Cell size of the source in meters; the detail starts at twice it, the source's Nyquist "
       "wavelength.",
       "m", Above(0.0));
    io("slope", options.slope,
       "Spectral slope beta of the radially averaged power spectrum, P(f) ~ f^-beta: beta = 2 H "
       "+ 2 for Hurst exponent H.",
       "", Between(2.0, 4.0));
    io("octave_rms_m", options.octaveRms,
       "RMS height in meters the power law holds at wavelengths from one to two source pixels.",
       "m", AtLeast(0.0));
    SpectralSynthesisFields(io, options.minWavelength, options.taperOctaves, options.seed,
                            options.fade);
}

}  // namespace terraingen
