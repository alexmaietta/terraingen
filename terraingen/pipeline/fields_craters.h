/// @file
/// @brief The JSON fields of crater and secondary crater layers and rim smoothing.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/fields_common.h"

#include <limits>
#include <string_view>

namespace terraingen::config {

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

template <typename IO> void Fields(IO& io, CraterSpacing& spacing) {
    io("diameter_ratio", spacing.diameterRatio,
       "Largest ratio of two craters' diameters, at least 1, for them to count as similar in size "
       "and be kept apart.");
    io("separation", spacing.separation,
       "Least distance between the centres of similar-size craters, in larger diameters; 0 "
       "disables spacing.");
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

template <typename IO> void Fields(IO& io, ValueDistribution& distribution) {
    io("kind", distribution.kind, "Kind of distribution the value is drawn from.");
    io("a", distribution.a,
       "The constant value, the uniform's lower bound, the normal's mean or the log-normal's "
       "median.");
    io("b", distribution.b,
       "The uniform's upper bound, or the standard deviation (of the natural logarithm for a "
       "log-normal); unused for a constant.");
    OptionalBound(io, "min", distribution.min,
                  "Lower bound drawn values are clamped to; unset leaves them unbounded below.", {},
                  -std::numeric_limits<double>::infinity());
    OptionalBound(io, "max", distribution.max,
                  "Upper bound drawn values are clamped to; unset leaves them unbounded above, "
                  "which normal and log-normal kinds do not allow.");
}

template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity) {
    io("enabled", irregularity.enabled,
       "Gives eligible craters irregular, non-circular outlines; off keeps every crater round.");
    io("seed", irregularity.seed,
       "Seed the outlines are drawn with, along with each crater's ID; changing it reshapes every "
       "irregular crater.");
    CraterSelection& selection = irregularity.selection;
    io("min_diameter", selection.minDiameter,
       "Smallest diameter, in metres, of craters made irregular.", "m");
    OptionalBound(io, "max_diameter", selection.maxDiameter,
                  "Largest diameter, in metres, of craters made irregular; unset has no upper "
                  "limit.",
                  "m");
    io("min_age_gyr", selection.minAgeGyr, "Youngest age, in Gyr, of craters made irregular.",
       "Gyr");
    OptionalBound(io, "max_age_gyr", selection.maxAgeGyr,
                  "Oldest age, in Gyr, of craters made irregular; unset has no upper limit.",
                  "Gyr");
    io("manual", selection.manual,
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

template <> struct SpecName<CraterLayerSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Impact craters generated from a density and age model, or placed by hand, and cut into "
        "the terrain.";
};
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

template <> struct SpecName<SecondaryLayerSpec> {
    static constexpr std::string_view kName = "secondaries";
    static constexpr std::string_view kDescription =
        "Secondary craters around the craters of the last crater layer before this one, after "
        "Singer et al. (2020).";
};
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
    OptionalBound(io, "max_diameter", o.maxDiameter,
                  "Largest secondary generated, in metres; unset has no limit beyond Singer et "
                  "al.'s largest secondary at each range.",
                  "m");
    io("outer_range", o.outerRange,
       "How far secondaries reach, in primary diameters from its centre; unset uses the longest "
       "typical ray of Elliott et al. (2018).");
    io("min_primary_diameter", o.selection.minDiameter,
       "Smallest primary crater, in metres, that throws secondaries.", "m");
    OptionalBound(io, "max_primary_diameter", o.selection.maxDiameter,
                  "Largest primary crater, in metres, that throws secondaries; unset has no upper "
                  "limit.",
                  "m");
    io("min_age_gyr", o.selection.minAgeGyr, "Youngest primary, in Gyr, that throws secondaries.",
       "Gyr");
    OptionalBound(io, "max_age_gyr", o.selection.maxAgeGyr,
                  "Oldest primary, in Gyr, that throws secondaries; unset has no upper limit.",
                  "Gyr");
    io("manual", o.selection.manual,
       "Lets manual primary craters throw secondaries too; otherwise only generated ones do.");
    io("cell_size_factor", o.cellSizeFactor,
       "Side of the polar cells secondaries are drawn in, as a multiple of each size band's "
       "largest diameter.");
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter every secondary is given, with no wear by age; lower values give "
       "shallower secondaries.");
    io("body", spec.body, "Body whose fresh crater rim the secondaries are shaped with.");
}

template <> struct SpecName<RimSmoothing> {
    static constexpr std::string_view kName = "rim_smoothing";
    static constexpr std::string_view kDescription =
        "Rounds the sharp rims of placed craters by blending towards a Gaussian-smoothed copy "
        "around each rim.";
};
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

}  // namespace terraingen::config
