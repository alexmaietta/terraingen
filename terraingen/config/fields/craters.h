/// @file
/// @brief The JSON fields of crater and secondary crater layers and rim smoothing.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/maps.h"

#include <limits>
#include <string_view>

namespace terraingen::config {

template <typename IO> void Fields(IO& io, Crater& crater) {
    io("x", crater.x,
       "Centre along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("y", crater.y,
       "Centre along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("diameter", crater.diameter, "Rim-to-rim diameter, in metres.", "m", Above(0.0));
    io("age_gyr", crater.ageGyr,
       "Age in Gyr; older craters are more degraded under a degradation model and are applied "
       "earlier in oldest-first order.",
       "Gyr", AtLeast(0.0));
    io("id", crater.id,
       "Stable ID that seeds the crater's outline; 0 derives one from its position and diameter.");
}

template <typename IO> void Fields(IO& io, CraterSpacing& spacing) {
    io("diameter_ratio", spacing.diameterRatio,
       "Largest ratio of two craters' diameters, at least 1, for them to count as similar in size "
       "and be kept apart.",
       "", AtLeast(1.0));
    io("separation", spacing.separation,
       "Least distance between the centres of similar-size craters, in larger diameters; 0 "
       "disables spacing.",
       "", AtLeast(0.0));
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

template <typename IO> void Fields(IO& io, CraterBlend& blend) {
    io("start_radii", blend.startRadii,
       "Distance from the centre, in crater radii, up to which the interior is fully replaced.", "",
       AtLeast(0.0));
    io("end_radii", blend.endRadii,
       "Distance from the centre, in crater radii, from which the surrounding terrain is draped "
       "unchanged.",
       "", Above(0.0));
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
    io("edges", distribution.edges,
       "With piecewise, the edges of the bins, at least two and strictly increasing; values are "
       "uniform within each bin. Empty for every other kind.");
    io("weights", distribution.weights,
       "With piecewise, the relative weight of each bin, one fewer than the edges, not negative "
       "and not all zero; each bin draws its share of the total. Empty for every other kind.",
       "", AtLeast(0.0));
}

/// The fields of irregular craters' rough, wavy rims.
template <typename IO> void RoughRimFields(IO& io, CraterIrregularity& irregularity) {
    io("rough_share", irregularity.roughShare,
       "Share of eligible craters, from 0 to 1, given rough, wavy rims.", "", UnitInterval());
    io("model", irregularity.model, "How rough rims are drawn.");
    io("scatter", irregularity.scatter,
       "With du_2024, scatters each crater's spectrum by the fits' measured misfits and noise.");
    io("max_deviation", irregularity.maxDeviation,
       "With du_2024, the largest deviation of a rim from a circle, in radii; outlines beyond it "
       "are scaled down to it.",
       "", AboveUpTo(0.0, 0.5));
    io("roughness", irregularity.roughness,
       "With power_law, the RMS deviation of a rough rim from a circle, in crater radii, drawn per "
       "crater.");
    io("spectral_slope", irregularity.spectralSlope,
       "With power_law, the exponent s of the rim harmonics' amplitudes, falling as k^-s, drawn "
       "per crater; higher values leave less fine waviness.");
    io("min_harmonic", irregularity.minHarmonic,
       "With power_law, the lowest harmonic of the rim's waviness, from 1 to 64.", "",
       Between(1.0, 64.0));
    io("max_harmonic", irregularity.maxHarmonic,
       "With power_law, the highest harmonic of the rim's waviness, at most 64; higher harmonics "
       "add finer wiggles.",
       "", Between(1.0, 64.0));
}

/// The fields of irregular craters stretched into ellipses.
template <typename IO> void ElongationFields(IO& io, CraterIrregularity& irregularity) {
    io("elongated_share", irregularity.elongatedShare,
       "Share of eligible craters, from 0 to 1, made elliptical, keeping their area.", "",
       UnitInterval());
    io("ellipticity", irregularity.ellipticity,
       "Long over short rim-to-rim diameter of elliptical craters, drawn per crater; at least 1.");
    RadianField(io, "heading_deg", irregularity.heading,
                "Heading of elliptical craters' long axes, in degrees counter-clockwise from +x "
                "(local east on a body); unset gives random headings.");
    RadianField(io, "heading_spread_deg", irregularity.headingSpread,
                "Largest random turn of a long axis either way from the heading, in degrees.",
                AtLeast(0.0));
}

/// The fields of the radial marks down irregular craters' walls.
template <typename IO> void RadialMarkFields(IO& io, CraterIrregularity& irregularity) {
    io("marks_share", irregularity.marksShare,
       "Share of eligible craters, from 0 to 1, given radial marks: ridges and grooves down the "
       "wall, strongest where it is steepest.",
       "", UnitInterval());
    io("marks_amplitude", irregularity.marksAmplitude,
       "Largest radial shift of the wall by the marks, in crater radii, drawn per crater; below "
       "marks_fade_radii / 3.75.");
    io("marks_knots", irregularity.marksKnots,
       "Random shifts around the crater the marks pass through, from 3 to 1024; more knots give "
       "narrower marks.",
       "", Between(3.0, 1024.0));
    io("marks_inner_radii", irregularity.marksInnerRadii,
       "Distance from the centre, in crater radii, where the marks reach full strength.", "",
       Inside(0.0, 1.0));
    io("marks_outer_radii", irregularity.marksOuterRadii,
       "Distance from the centre, in crater radii, where the marks start to fade; at most 1 minus "
       "marks_fade_radii.",
       "", Inside(0.0, 1.0));
    io("marks_fade_radii", irregularity.marksFadeRadii,
       "Width, in crater radii, over which the marks fade in and out.", "", AboveUpTo(0.0, 0.5));
}

template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity) {
    io("enabled", irregularity.enabled,
       "Gives eligible craters irregular, non-circular outlines; off keeps every crater round.");
    io("seed", irregularity.seed,
       "Seed the outlines are drawn with, along with each crater's ID; changing it reshapes every "
       "irregular crater.");
    SelectionFields(io, irregularity.selection, "craters made irregular",
                    "Makes manual craters irregular too; otherwise only generated craters are.");
    RoughRimFields(io, irregularity);
    ElongationFields(io, irregularity);
    RadialMarkFields(io, irregularity);
}

template <typename IO> void Fields(IO& io, RimSmoothing& smoothing) {
    io("sigma_radii", smoothing.sigmaRadii,
       "Deviation of the Gaussian the profile is smoothed with across distance, in crater radii; "
       "larger values round rims more, and 0 leaves them sharp.",
       "", AtLeast(0.0));
    io("inner_radii", smoothing.innerRadii,
       "Inner edge of the smoothed band around the rim, in crater radii; below 1.", "",
       FromBelow(0.0, 1.0));
    io("outer_radii", smoothing.outerRadii,
       "Outer edge of the smoothed band around the rim, in crater radii; above 1.", "", Above(1.0));
    io("shape", smoothing.shape,
       "How the smoothing fades from full at the rim to none at the band's edges.");
    io("strength", smoothing.strength,
       "Share of the smoothed height used at the rim, from 0 to 1; lower values keep more of the "
       "sharp crest.",
       "", UnitInterval());
}

template <typename IO> void Fields(IO& io, CraterNoise& noise) {
    io("amplitude", noise.amplitude,
       "Amplitude of the noise over the whole footprint, as a share of the crater diameter; it "
       "fades out beyond the rim to the profile's reach, and 0 adds none.",
       "", AtLeast(0.0));
    io("wavelength_radii", noise.wavelengthRadii,
       "Wavelength of the footprint noise's first octave, in crater radii.", "", Above(0.0));
    io("octaves", noise.octaves, "Octaves of both noises, from 1 to 12.", "", Between(1.0, 12.0));
    io("gain", noise.gain, "Weight ratio between successive octaves; positive.", "", Above(0.0));
    io("rim_amplitude", noise.rimAmplitude,
       "Amplitude of the extra noise in the band around the rim, as a share of the crater "
       "diameter; 0 adds none.",
       "", AtLeast(0.0));
    io("rim_wavelength_radii", noise.rimWavelengthRadii,
       "Wavelength of the rim noise's first octave, in crater radii.", "", Above(0.0));
    io("rim_inner_radii", noise.rimInnerRadii,
       "Inner edge of the rim noise band, in crater radii; below 1.", "", FromBelow(0.0, 1.0));
    io("rim_outer_radii", noise.rimOuterRadii,
       "Outer edge of the rim noise band, in crater radii; above 1.", "", Above(1.0));
    io("seed", noise.seed, "Varies the noise of every crater.");
}

template <typename IO> void Fields(IO& io, CraterTexture& texture) {
    io("wall_streaks", texture.wallStreaks,
       "Amplitude of the radial gullies down a fresh crater's inner wall, as a share of its "
       "diameter; 0 adds none.",
       "", AtLeast(0.0));
    io("wall_streak_width_radii", texture.wallStreakWidthRadii,
       "Width of the gullies across the wall, in crater radii.", "", Above(0.0));
    io("ejecta", texture.ejecta,
       "Amplitude of the hummocky, radially lineated ejecta around a fresh crater, as a share of "
       "the ejecta's thickness (McGetchin et al. 1973); 0 adds none.",
       "", AtLeast(0.0));
    io("ejecta_lineation_radii", texture.ejectaLineationRadii,
       "Width of the ejecta's lineations across, in crater radii at the rim.", "", Above(0.0));
    io("mantling", texture.mantling,
       "Share of older relief buried under the ejecta at the rim, falling with their thickness, "
       "from 0 to 1; needs a datum.",
       "", UnitInterval());
    io("ejecta_reach_radii", texture.ejectaReachRadii,
       "How far ejecta texture and burial reach, in crater radii; above 1.", "", Above(1.0));
    io("min_freshness", texture.minFreshness,
       "Freshness (depth over fresh depth) at or below which gullies and ejecta texture are worn "
       "away, from 0 to below 1.",
       "", FromBelow(0.0, 1.0));
    io("seed", texture.seed, "Varies the texture of every crater.");
}

template <typename IO> void Fields(IO& io, CraterSlopeResponse& slope) {
    io("enabled", slope.enabled,
       "Shapes craters asymmetrically along the slope of the ground beneath the layer; off keeps "
       "them the same on any slope.");
    io("seed", slope.seed, "Varies how far each crater's floor is offset.");
    io("floor_offset", slope.floorOffset,
       "How far the deepest point lies downslope of the centre, in diameters, drawn per crater; "
       "below 0.23.");
    io("upslope_rim", slope.upslopeRim,
       "Share of the raised rim and ejecta kept straight upslope, from 0 to 1; the rest returns "
       "toward the sides.",
       "", UnitInterval());
    RadianField(io, "full_slope_deg", slope.fullSlope,
                "Slope at which the asymmetry is complete, in degrees; it rises from none on "
                "level ground.",
                Above(0.0));
    io("critical_gradient", slope.criticalGradient,
       "Critical gradient Sc, rise over run, toward which downslope transport grows without bound: "
       "a crater on a regional gradient S wears 1 / (1 - (S / Sc)^2) times as fast (Roering et al. "
       "1999). Unset wears craters alike on any slope; no lunar value is published.",
       "", Above(0.0));
    io("max_wear_rate", slope.maxWearRate,
       "Fastest a slope makes craters wear, over level ground: a numerical cap as S nears Sc.", "",
       AtLeast(1.0));
}

template <typename IO> void Fields(IO& io, CraterApplyOptions& apply) {
    io("order", apply.order,
       "Order in which overlapping craters are applied; later craters overprint earlier ones.");
    io("fade", apply.fade, "How craters too small to resolve at the sample spacing fade out.");
    io("datum", apply.datum,
       "Reference surface each crater's interior is cut relative to, fitted to the ground beneath "
       "it; none adds craters onto the terrain everywhere.");
    io("datum_fit_radii", apply.datumFitRadii,
       "Radius of the footprint the datum is fitted over, in crater radii.", "", Above(0.0));
    io("interior_replacement", apply.interiorReplacement,
       "Share of the interior replaced by datum plus profile, from 0 to 1: 1 erases older relief "
       "inside the crater, 0 keeps it.",
       "", UnitInterval());
    io("blend", apply.blend,
       "Where and how a crater's replaced interior blends into the surrounding terrain.");
    io("irregularity", apply.irregularity,
       "Irregular, non-circular crater outlines: which craters have them and how their shapes "
       "are drawn.");
    io("rim_smoothing", apply.rimSmoothing,
       "Rounded rims: each crater's profile smoothed across distance near its rim; off unless "
       "sigma_radii is set.");
    io("noise", apply.noise,
       "Fractal noise over each crater's footprint and around its rim, keyed to the crater; off "
       "unless an amplitude is set.");
    io("texture", apply.texture,
       "Gullies down fresh walls, textured ejecta and older relief buried under the ejecta; off "
       "unless an amplitude or mantling is set.");
    io("slope", apply.slope,
       "Craters on sloping ground with floors offset downslope and rims lowered upslope; needs "
       "the plane datum.");
}

template <> struct SpecName<CraterLayerSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Impact craters generated from a density and age model, or placed by hand, and cut into "
        "the terrain.";
};
template <typename IO> void Fields(IO& io, CraterLayerSpec& craters) {
    io("morphometry", craters.morphometry,
       "Published fits of fresh crater depth, rim height and complex crater shape the craters "
       "are shaped with.");
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
       "Smallest crater generated, in metres; lower values add many more small craters.", "m",
       Above(0.0));
    io("max_diameter", craters.maxDiameter,
       "Largest crater generated, in metres; above the smallest.", "m", Above(0.0));
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
    io("density_map", craters.densityMap,
       "Map that sets the crater density point by point, scaling the density's size distribution "
       "to what it measures; unset keeps the density the same everywhere.");
    io("completeness", craters.completeness,
       "Where the manual craters are a complete catalog: the diameter in metres from which they "
       "stand in for generated craters, infinite or no value where there is no catalog. Unset "
       "keeps every generated crater.");
}

template <typename IO> void Fields(IO& io, RayLayout& layout) {
    io("seed", layout.seed,
       "Seed of the rays' directions; layers with the same layout share the same rays.");
    io("count", layout.count, "Number of rays of a crater, drawn per crater and rounded; 0 to 64.");
    io("width_deg", layout.widthDegrees,
       "Angle in degrees a ray spans 2 radii from the centre, drawn per ray, within (0, 180).");
}

template <typename IO> void Fields(IO& io, SecondaryRays& rays) {
    io("enabled", rays.enabled,
       "Gathers secondaries into their primary's rays and clumps them along each ray, as radial "
       "chains and clusters; off spreads them evenly.");
    io("layout", rays.layout,
       "The rays; give a crater albedo layer's rays the same layout for bright rays over the "
       "secondaries.");
    io("share", rays.share,
       "Share of secondaries in rays, from 0 to 1; the rest stay spread evenly. Illustrative.", "",
       UnitInterval());
    io("min_half_angle_deg", rays.minHalfAngleDegrees,
       "Narrowest half width of a ray, in degrees from 0 to 90 excluded, so rays widen with range "
       "beyond their width near the primary. Illustrative.",
       "deg", Inside(0.0, 90.0));
    io("clustering", rays.clustering,
       "How strongly secondaries clump along a ray, from 0 for evenly along it to 1 for only in "
       "clumps. Illustrative.",
       "", UnitInterval());
    io("cluster_spacing", rays.clusterSpacing,
       "Typical spacing of clumps along a ray, in natural log of range; positive, smaller values "
       "give shorter, more frequent clumps. Illustrative.",
       "", Above(0.0));
}

template <typename IO> void Fields(IO& io, SecondaryShapes& shapes) {
    io("elongated_share", shapes.elongatedShare,
       "Share of secondaries, from 0 to 1, stretched along the line back to their primary; 0 keeps "
       "them round.",
       "", UnitInterval());
    io("ellipticity", shapes.ellipticity,
       "Long over short axis of an elongated secondary near the primary, drawn per secondary; at "
       "least 1, falling to 1 at the outer range.");
}

template <typename IO> void Fields(IO& io, SecondaryCraterOptions& o) {
    io("seed", o.seed,
       "Seed the secondaries are drawn with; changing it gives a different field of the same "
       "statistics.");
    io("onset_count", o.onsetCount,
       "Secondaries at least count_fraction primary diameters across, in an area the size of the "
       "primary at the onset range. Required: the sources give sizes, not numbers.",
       "", Above(0.0));
    io("count_fraction", o.countFraction,
       "Size, as a fraction of the primary's diameter, from which the onset count counts "
       "secondaries.",
       "", Above(0.0));
    io("size_slope", o.sizeSlope,
       "Cumulative size slope of the secondaries, their number falling as d^-slope; higher values "
       "give relatively more small secondaries.",
       "", Above(0.0));
    io("min_diameter", o.minDiameter, "Smallest secondary generated, in metres.", "m", Above(0.0));
    OptionalBound(io, "max_diameter", o.maxDiameter,
                  "Largest secondary generated, in metres; unset has no limit beyond Singer et "
                  "al.'s largest secondary at each range.",
                  "m", std::numeric_limits<double>::infinity(), Above(0.0));
    io("outer_range", o.outerRange,
       "How far secondaries reach, in primary diameters from its centre; unset uses the longest "
       "typical ray of Elliott et al. (2018).",
       "", Above(0.0));
    SelectionKeys primaryKeys;
    primaryKeys.minDiameter = "min_primary_diameter";
    primaryKeys.maxDiameter = "max_primary_diameter";
    SelectionFields(
        io, o.selection, "primary craters that throw secondaries",
        "Lets manual primary craters throw secondaries too; otherwise only generated ones do.",
        primaryKeys);
    io("cell_size_factor", o.cellSizeFactor,
       "Side of the polar cells secondaries are drawn in, as a multiple of each size band's "
       "largest diameter.",
       "", Above(0.0));
    io("rays", o.rays, "Secondaries gathered into rays and clumped along them; off by default.");
    io("shapes", o.shapes, "Secondaries stretched toward their primary; round by default.");
}

template <> struct SpecName<SecondaryLayerSpec> {
    static constexpr std::string_view kName = "secondaries";
    static constexpr std::string_view kDescription =
        "Secondary craters around the craters of the last crater layer before this one, after "
        "Singer et al. (2020).";
};
template <typename IO> void Fields(IO& io, SecondaryLayerSpec& spec) {
    Fields(io, spec.secondaries);
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter every secondary is given, with no wear by age; lower values give "
       "shallower secondaries.",
       "", Above(0.0));
    io("morphometry", spec.morphometry,
       "Published fits whose fresh crater rim the secondaries are shaped with.");
}

}  // namespace terraingen::config
