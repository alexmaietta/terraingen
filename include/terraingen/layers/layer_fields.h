// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Field lists shared by layers: fades, exposure, selections, anchors, raster fields,
/// density maps, roughness and spectral continuation, and the names of their enums.
#pragma once

#include "terraingen/layers/density_map.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/raster/raster_fields.h"
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

/// The fields of a resolution fade (`ResolutionFade`) as "fade_px", its start and end sample
/// counts as a list of two in output samples (pixels), and "fade_shape", the curve between them.
/// @param description What the counts mean for the features that fade.
template <typename IO>
void FadePixelFields(IO& io, ResolutionFade& fade, std::string_view description) {
    if constexpr (BindsNative<IO>) {
        if (io.Group("fade", fade, description)) {
            return;
        }
    }
    std::array<double, 2> pixels{fade.startSamples, fade.endSamples};
    io("fade_px", pixels, description, "px", AtLeast(0.0));
    fade.startSamples = pixels[0];
    fade.endSamples = pixels[1];
    io("fade_shape", fade.shape, "Curve of the fade between the two sample counts of fade_px.");
}

/// The field "source_resolves_px": source cells across from which the source's measured data
/// already holds the `features`, measured by their `size`, so only smaller ones are added.
template <typename IO>
void SourceResolvesField(IO& io, std::optional<double>& pixels, std::string_view features,
                         std::string_view size) {
    io("source_resolves_px", pixels,
       "Source cells across from which the source's data already holds the " +
           std::string(features) + ", by their " + std::string(size) +
           ": only smaller ones are added, each raster of a chain giving its own limit. "
           "Ignored on synthetic sources and where no raster has data; null adds them all.",
       "px", Above(0.0));
}

/// The keys of a crater selection's fields.
struct SelectionKeys {
    std::string_view minDiameter = "min_diameter_m";
    std::string_view maxDiameter = "max_diameter_m";
    std::string_view minAge = "min_age_gyr";
    std::string_view maxAge = "max_age_gyr";
    std::string_view manual = "manual";
};

/// The fields of a crater selection (`CraterSelection`): the diameters and ages of the craters
/// selected, and whether manual craters are.
/// @param craters What the selected craters are, such as "craters made irregular".
/// @param manualHelp The description of the manual flag.
template <typename IO, typename Selection>
void SelectionFields(IO& io, Selection& selection, std::string_view craters,
                     std::string_view manualHelp, const SelectionKeys& keys = {}) {
    if constexpr (BindsNative<IO>) {
        if (io.Group("selection", selection,
                     "The selection of " + std::string(craters) +
                         ": their diameters and ages, and whether manual craters count.")) {
            return;
        }
    }
    const std::string of = ", of " + std::string(craters);
    io(keys.minDiameter, selection.minDiameter, "Smallest diameter, in meters" + of + ".", "m",
       AtLeast(0.0));
    OptionalBound(io, keys.maxDiameter, selection.maxDiameter,
                  "Largest diameter, in meters" + of + "; unset has no upper limit.", "m",
                  std::numeric_limits<double>::infinity(), AtLeast(0.0));
    io(keys.minAge, selection.minAgeGyr, "Youngest age, in Gyr" + of + ".", "Gyr", AtLeast(0.0));
    OptionalBound(io, keys.maxAge, selection.maxAgeGyr,
                  "Oldest age, in Gyr" + of + "; unset has no upper limit.", "Gyr",
                  std::numeric_limits<double>::infinity(), AtLeast(0.0));
    io(keys.manual, selection.manual, manualHelp);
}

/// The description of a wind azimuth: where `wind`, such as "wind", blows toward, and the
/// `effect` it has, such as "slip faces face it".
inline std::string WindAzimuthHelp(std::string_view wind, std::string_view effect) {
    return "Direction the " + std::string(wind) +
           " blows toward, in degrees counter-clockwise from +x (local east on a body); " +
           std::string(effect) + ".";
}

/// The fields of the anchor of a layer laid out in a tangent frame: the longitude and latitude,
/// on a body, of the point whose tangent frame `what`, such as "the plates are laid out in".
/// @param prefix The keys' prefix, to which "_x" and "_y" are added.
template <typename IO, typename Anchor>
void AnchorFields(IO& io, Anchor& anchor, std::string_view what,
                  std::string_view prefix = "anchor") {
    if constexpr (BindsNative<IO>) {
        if (io.Group(prefix, anchor,
                     "On a body, the longitude and latitude in degrees of the point whose tangent "
                     "frame " +
                         std::string(what) + ".")) {
            return;
        }
    }
    io(std::string(prefix) + "_x", anchor.x,
       "On a body, the longitude in degrees of the point whose tangent frame " + std::string(what) +
           ".");
    io(std::string(prefix) + "_y", anchor.y, "On a body, the latitude in degrees of the anchor.");
}

/// The fields of an exposure field a layer reads on the ground beneath it, and the spacing in
/// meters it reads the ground at.
/// @param exposureHelp The description of the exposure field.
/// @param prefix The exposure's key, to which "_spacing_m" is added for the spacing's.
template <typename IO, typename Exposure>
void ExposureFields(IO& io, Exposure& exposure, double& spacing, std::string_view exposureHelp,
                    std::string_view prefix = "exposure") {
    io(std::string(prefix), exposure, exposureHelp);
    io(std::string(prefix) + "_spacing_m", spacing,
       "Spacing in meters the ground is read at for the exposure field.", "m", Above(0.0));
}

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

template <typename IO> void Fields(IO& io, RasterFieldOptions& options) {
    io("method", options.method,
       "How the raster is interpolated: nearest or bilinear, which stay within the cells read.");
    io("scale", options.scale,
       "Multiplies every cell, such as 0.001 for a map stored in thousandths.", "", Above(0.0));
    OptionalBound(io, "fallback", options.fallback,
                  "Value outside the raster and where its cells are no-data or NaN; unset leaves "
                  "none, and the layer then keeps its own setting there.",
                  "", std::numeric_limits<double>::quiet_NaN());
    io("periodic_longitude", options.periodicLongitude,
       "The raster's x is longitude repeating every 360 degrees, so a map from 0 to 360 serves "
       "-180 to 180.");
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
