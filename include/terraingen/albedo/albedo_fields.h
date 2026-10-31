// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of the albedo options and Hapke parameters.
#pragma once

#include "terraingen/albedo/blast_zones.h"
#include "terraingen/albedo/crater_albedo_options.h"
#include "terraingen/albedo/photometry.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/rock_albedo.h"
#include "terraingen/albedo/slope_albedo.h"
#include "terraingen/albedo/splotches.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/random/random_fields.h"
#include "terraingen/util/option_fields.h"

namespace terraingen {

template <typename IO> void Fields(IO& io, AlbedoDetail& detail) {
    io("enabled", detail.enabled,
       "Adds fractal detail below the map's resolution; off leaves the interpolated map as it "
       "is.");
    io("seed", detail.seed,
       "Seed of the detail noise; changing it gives a different pattern of the same roughness.");
    io("octaves", detail.octaves,
       "Octaves of detail, from half the map's cell size down, each half the wavelength of the "
       "one before.",
       "", Between(1.0, 16.0));
    io("scale", detail.scale,
       "Factor on the RMS of every octave; 0 adds no detail, higher values give a more mottled "
       "albedo.",
       "", AtLeast(0.0));
    io("hurst", detail.hurst,
       "Hurst exponent used instead of the one measured from the map; lower values "
       "keep more contrast at fine scales. Unset uses the measured one.",
       "", UnitInterval());
    FadePixelFields(io, detail.fade,
                    "How octaves of detail too fine for the sample spacing fade out.");
}

template <typename IO> void Fields(IO& io, ObliqueImpacts& oblique) {
    io("enabled", oblique.enabled,
       "Draws an impact angle and direction per crater that shape its rays; off treats every "
       "impact as vertical.");
    io("heading_deg", oblique.headingDegrees,
       "Downrange direction of every impact, in degrees counter-clockwise from +x (local east on "
       "a body); unset draws a random heading per crater.",
       "deg");
    io("asymmetric_deg", oblique.asymmetricDegrees,
       "Impact angle from the horizontal, in degrees, below which ejecta leave faster downrange, "
       "lengthening rays there and shortening them uprange.",
       "deg", Between(0.0, 90.0));
    io("forbidden_deg", oblique.forbiddenDegrees,
       "Impact angle in degrees below which rays within the zone half width of uprange are "
       "dropped; at most the asymmetric angle.",
       "deg", Between(0.0, 90.0));
    io("butterfly_deg", oblique.butterflyDegrees,
       "Impact angle in degrees below which rays within the zone half width of downrange are "
       "dropped too, a butterfly pattern; at most the forbidden angle.",
       "deg", Between(0.0, 90.0));
    io("downrange_speed_ratio", oblique.downrangeSpeedRatio,
       "Downrange over uprange ejection speed below the asymmetric angle, drawn per crater; at "
       "least 1, higher values stretch downrange rays more.");
    io("zone_half_width_deg", oblique.zoneHalfWidthDegrees,
       "Half width in degrees of the ray-free zones uprange and downrange; "
       "illustrative, as the sources do not measure it.",
       "deg", Between(0.0, 180.0));
}

template <typename IO> void Fields(IO& io, CraterRays& rays) {
    io("enabled", rays.enabled,
       "Gives craters rays of bright material; off leaves only their interiors and ejecta "
       "bright.");
    io("count", rays.count,
       "Number of rays of a crater, drawn per crater and rounded; from 0 to 64.");
    io("length_coefficient", rays.lengthCoefficient,
       "Coefficient of the median ray length, coefficient R^exponent km for crater radius R in "
       "km; higher values give longer rays.",
       "", Above(0.0));
    io("length_exponent", rays.lengthExponent,
       "Exponent of the crater radius in the median ray length; higher values make large "
       "craters' rays relatively longer.",
       "", Above(0.0));
    io("length_spread", rays.lengthSpread,
       "Natural logarithm of a ray's length over the median, drawn per ray; a wider "
       "distribution gives more varied lengths.");
    io("width_deg", rays.widthDegrees,
       "Angle in degrees a ray spans 2 radii from the center, drawn per ray, within (0, 180); "
       "beyond 2 radii a ray keeps the width it has there.");
    io("excess", rays.excess,
       "Optical maturity above the background along a ray at the rim, falling linearly to 0 at "
       "its end; higher values give brighter rays.",
       "", AtLeast(0.0));
    io("patchiness", rays.patchiness,
       "Share of a ray's strength that noise along it can take away, breaking it into patches; 0 "
       "gives unbroken rays.",
       "", UnitInterval());
    io("patch_radii", rays.patchRadii,
       "Wavelength of the noise along a ray, in crater radii; smaller values give shorter "
       "patches.",
       "", Above(0.0));
    io("oblique", rays.oblique,
       "How oblique impacts lengthen rays downrange and clear them uprange.");
    io("layout", rays.layout,
       "Takes the rays' number, directions and widths from this layout in place of count and "
       "width_deg, so they lie over a secondaries layer with the same layout; unset draws them "
       "per crater.");
}

template <typename IO> void Fields(IO& io, RayRetention& retention) {
    io("small_diameter_m", retention.smallDiameter,
       "Diameter in meters at or below which craters keep their bright material for the small "
       "time.",
       "m", Above(0.0));
    io("small_myr", retention.smallMyr,
       "Time in Myr craters up to the small diameter keep their bright material.", "Myr",
       Above(0.0));
    io("mid_diameter_m", retention.midDiameter,
       "Diameter in meters from which craters keep their bright material for the mid time; "
       "between the small and mid diameters the time follows a power law of diameter.",
       "m", Above(0.0));
    io("mid_myr", retention.midMyr,
       "Time in Myr craters from the mid to the large diameter keep their bright material.", "Myr",
       Above(0.0));
    io("large_diameter_m", retention.largeDiameter,
       "Diameter in meters above which craters keep their bright material for the large time.", "m",
       Above(0.0));
    io("large_myr", retention.largeMyr,
       "Time in Myr craters above the large diameter keep their bright material.", "Myr",
       Above(0.0));
    io("scale", retention.scale,
       "Factor every retention time is multiplied by; higher values keep craters bright for "
       "longer.",
       "", Above(0.0));
}

template <typename IO> void Fields(IO& io, CraterAlbedoTexture& texture) {
    io("streaks", texture.streaks,
       "Share of the excess radial streaks take away between filaments, breaking ejecta, rays and "
       "interiors into fine streaks; 0 leaves them smooth.",
       "", UnitInterval());
    io("streak_width_deg", texture.streakWidthDegrees,
       "Typical width of a filament around the crater, in degrees.", "deg", Above(0.0));
    io("streak_length_radii", texture.streakLengthRadii,
       "Typical length of a filament outward from the crater, in crater radii.", "", Above(0.0));
    io("continuous_radii", texture.continuousRadii,
       "How far beyond the rim the ejecta stay continuous before breaking into filaments, in "
       "crater radii; 0 streaks them from the rim.",
       "", AtLeast(0.0));
    io("lobes", texture.lobes,
       "How far the ejecta's edge wanders in and out, as a share of its reach; 0 keeps it "
       "circular.",
       "", Between(0.0, 0.9));
    io("lobe_count", texture.lobeCount, "Typical number of lobes around a crater.", "",
       Between(1.0, 64.0));
    io("floor_share", texture.floorShare,
       "Interior excess on the floor over that at the rim; 1 keeps the interior "
       "even, lower values brighten the walls over the floor.",
       "", UnitInterval());
    io("seed", texture.seed, "Varies every crater's texture.");
}

template <typename IO> void Fields(IO& io, ReflectanceZone& zone) {
    io("share", zone.share, "Share of craters with the zone.", "", UnitInterval());
    io("change", zone.change,
       "Reflectance change relative to the pre-impact surface, drawn per crater; above -1, "
       "negative values darken.");
    io("coefficient_m", zone.coefficient,
       "Coefficient of the zone's reach from the crater's center, coefficient D^exponent meters "
       "for diameter D in meters.",
       "m", Above(0.0));
    io("exponent", zone.exponent, "Exponent of the diameter in the zone's reach.", "", Above(0.0));
    io("scale", zone.scale,
       "Factor on the reach, drawn per crater; positive and bounded, a wider distribution gives "
       "more varied zones.");
    io("raggedness", zone.raggedness,
       "Share of the reach that fingers of the edge pull in by; 0 keeps the zone round, higher "
       "values make it ray-like.",
       "", FromBelow(0.0, 1.0));
    io("finger_width_deg", zone.fingerWidthDegrees,
       "Typical width of the edge's fingers around the crater, in degrees.", "deg", Above(0.0));
    io("feather", zone.feather, "Share of the reach over which the zone fades out at its edge.", "",
       AboveUpTo(0.0, 1.0));
    io("retention_myr", zone.retentionMyr,
       "How long the zone lasts, in Myr, fading like the bright material; unset lasts as long as "
       "the bright material.",
       "Myr", Above(0.0));
}

template <typename IO> void Fields(IO& io, ZoneShadows& shadows) {
    io("enabled", shadows.enabled,
       "Gives the distal zones shadows behind rises such as crater rims, read from the terrain's "
       "heights; off by default.");
    io("jet_angle_deg", shadows.jetAngleDegrees,
       "Steepest angle in degrees at which the jet forming the distal zones can drop behind a "
       "rise; lower values give longer shadows. Illustrative.",
       "deg", Inside(0.0, 90.0));
    io("depth_m", shadows.depth,
       "Depth in meters of the ground below the jet at which the shadow is full. Illustrative.",
       "m", Above(0.0));
    io("azimuths", shadows.azimuths, "Rays around a crater the ground is sampled on.", "",
       Between(8.0, 8192.0));
    io("radial_samples", shadows.radialSamples,
       "Samples along each ray out to the distal zones' reach.", "", Between(8.0, 8192.0));
}

template <typename IO> void Fields(IO& io, CraterReflectanceZones& zones) {
    io("enabled", zones.enabled,
       "Gives new small craters the reflectance zones of LROC temporal pairs; off by default.");
    io("max_diameter_m", zones.maxDiameter,
       "Largest crater diameter, in meters, with zones; the published fits stop near 43 m.", "m",
       Above(0.0));
    io("mature_depth_m", zones.matureDepth,
       "Depth in meters of mature regolith; only craters excavating deeper get the proximal "
       "high reflectance zone.",
       "m", AtLeast(0.0));
    io("excavation_ratio", zones.excavationRatio, "Excavation depth over crater diameter.", "",
       Above(0.0));
    io("proximal_high", zones.proximalHigh,
       "Proximal high reflectance zone: immature ejecta, +12% to a median 1.5 D.");
    io("proximal_low", zones.proximalLow,
       "Proximal low reflectance zone: -9% to a median 2.9 D, around nearly every crater.");
    io("distal_high", zones.distalHigh,
       "Distal high reflectance zone: +1% to +4% to 0.52 D^2.5 m, around 24% of craters.");
    io("distal_low", zones.distalLow,
       "Distal low reflectance zone: -2% to -6%, ray-like, to 5.03 D^2 m, around 84% of "
       "craters.");
    io("shadows", zones.shadows, "Shadows of rises in the distal zones; off by default.");
    io("seed", zones.seed, "Varies every crater's zones.");
}

template <typename IO> void Fields(IO& io, CraterAlbedoOptions& options) {
    io("seed", options.seed,
       "Seed of which craters get bright material, their rays and their impacts; changing it "
       "gives a different pattern of the same statistics.");
    SelectionFields(
        io, options.selection, "craters given bright material",
        "Gives manual craters bright material too; otherwise only generated and replacing "
        "craters have it.");
    io("share", options.share, "Share of eligible craters given bright material.", "",
       UnitInterval());
    io("background_maturity", options.backgroundMaturity,
       "Optical maturity (OMAT) of the ground around the craters; the excesses are added to it "
       "and the albedo is brightened from it.",
       "", Above(0.0));
    io("max_maturity", options.maxMaturity,
       "Cap on the optical maturity where craters' excesses add up, at least the background; it "
       "limits how bright overlapping craters get.");
    io("interior_excess", options.interiorExcess,
       "Optical maturity above the background inside a fresh crater's rim; higher values give "
       "brighter interiors.",
       "", AtLeast(0.0));
    io("texture", options.texture,
       "Filaments, lobed ejecta edges and walls brighter than floors; off unless streaks, lobes "
       "or a floor share below 1 are set.");
    io("zones", options.zones,
       "Bright and dark reflectance zones around new small craters, after Robinson et al. "
       "(2015) and Speyerer et al. (2016); off by default.");
    io("ejecta_excess", options.ejectaExcess,
       "Optical maturity above the background on a fresh crater's continuous ejecta at the rim, "
       "falling with distance.",
       "", AtLeast(0.0));
    io("ejecta_exponent", options.ejectaExponent,
       "Exponent of the ejecta's fall with distance r, as (r / R)^-exponent for crater radius R; "
       "higher values keep the bright ejecta nearer the rim.",
       "", Above(0.0));
    io("ejecta_reach_radii", options.ejectaReachRadii,
       "Distance from the center, in crater radii, at which the ejecta's excess reaches 0.", "",
       Above(1.0));
    io("rays", options.rays, "Rays of bright material running out from the craters.");
    io("retention", options.retention,
       "How long craters of each diameter keep their bright material.");
    io("fade_exponent", options.fadeExponent,
       "Exponent of the fade with age t, max(0, 1 - t / T)^exponent for retention time T; 1 is "
       "a straight fade and higher values fade sooner.",
       "", Above(0.0));
    io("origin_reflectance", options.originReflectance,
       "Reflectance of the optical maturity origin, the hypermature end-member, from which "
       "reflectance scales with maturity at fixed iron content.");
    FadePixelFields(
        io, options.fade,
        "How bright material of craters too small to resolve at the sample spacing fades out.");
}

template <typename IO> void Fields(IO& io, SplotchSwarms& swarms) {
    io("enabled", swarms.enabled,
       "Gives new craters swarms of splotches, their ejecta's secondaries (Robinson et al. "
       "2015); off by default.");
    io("crater_rate", swarms.craterRate,
       "New craters at least 10 m across per km^2 per year; 4.84e-6 from Speyerer et al. (2016).",
       "", Above(0.0));
    io("crater_slope", swarms.craterSlope,
       "Cumulative size slope of the new craters; the Neukum production function has 2.97 to "
       "3.15.",
       "", Above(0.0));
    io("min_crater_diameter_m", swarms.minCraterDiameter,
       "Smallest crater with a swarm, in meters; smaller craters throw few splotches.", "m",
       Above(0.0));
    io("max_crater_diameter_m", swarms.maxCraterDiameter, "Largest crater with a swarm, in meters.",
       "m", Above(0.0));
    io("reference_diameter_m", swarms.referenceDiameter,
       "Diameter in meters of the crater whose swarm was counted: 18.8 m, Robinson et al.", "m",
       Above(0.0));
    io("reference_count", swarms.referenceCount,
       "Splotches of the reference crater: 248 within 30 km, Robinson et al.", "", Above(0.0));
    io("mu", swarms.mu,
       "Velocity exponent of the target; 0.41 for sand (Housen and Holsapple "
       "2011). Counts grow as D^(3 + 1.5 mu) and landing distances fall as r^(-1.5 mu).",
       "", Inside(0.0, 1.0));
    io("min_range_m", swarms.minRange,
       "Nearest landing distance in meters, setting Robinson et al.'s median of 8.7 km.", "m",
       Above(0.0));
    io("max_range_m", swarms.maxRange, "Farthest landing distance in meters.", "m", Above(0.0));
    io("splotch_diameter", swarms.splotchDiameter,
       "Diameter of a swarm splotch in meters, drawn per splotch: 2 to 22 m, median 6 m.");
    io("dark_share", swarms.darkShare, "Share of swarm splotches darker than the ground.", "",
       UnitInterval());
    io("bright_share", swarms.brightShare, "Share brighter than the ground; the rest are mixed.",
       "", UnitInterval());
    io("dark_change", swarms.darkChange, "Reflectance change of a dark swarm splotch.");
    io("bright_change", swarms.brightChange, "Reflectance change of a bright swarm splotch.");
    io("wedge_share", swarms.wedgeShare,
       "Share of swarm splotches shaped as wedges pointing back to their crater. Illustrative.", "",
       UnitInterval());
    io("wedge_elongation", swarms.wedgeElongation, "Long over short axis of a wedge. Illustrative.",
       "", AtLeast(1.0));
    io("craters", swarms.craters,
       "New craters given by hand, such as an observed impact, each throwing a swarm.");
}

template <typename IO> void Fields(IO& io, SplotchAlbedoOptions& options) {
    io("seed", options.seed, "Seed of where splotches fall and what they are.");
    io("rate_coefficient", options.rateCoefficient,
       "Coefficient a of the cumulative formation rate a d^-b per km^2 per year, d the diameter "
       "in km; higher values give more splotches.",
       "", Above(0.0));
    io("rate_exponent", options.rateExponent,
       "Slope b of the cumulative formation rate; higher values give relatively more small "
       "splotches.",
       "", Above(0.0));
    io("lifetime_myr", options.lifetimeMyr,
       "How long a splotch lasts in Myr, fading linearly; the default is the time splotches take "
       "to churn the top centimeters.",
       "Myr", Above(0.0));
    io("min_diameter_m", options.minDiameter,
       "Smallest splotch diameter in meters; the published fit is extrapolated below 10 m.", "m",
       Above(0.0));
    io("max_diameter_m", options.maxDiameter,
       "Largest splotch diameter in meters, above the smallest.", "m", Above(0.0));
    io("dark_share", options.darkShare, "Share of splotches darker than the ground.", "",
       UnitInterval());
    io("bright_share", options.brightShare,
       "Share of splotches brighter than the ground; the rest are mixed, dark on "
       "one side and bright on the other.",
       "", UnitInterval());
    io("dark_change", options.darkChange,
       "Reflectance change of a dark splotch relative to the ground before it, drawn per "
       "splotch; above -1.");
    io("bright_change", options.brightChange,
       "Reflectance change of a bright splotch, drawn per splotch; above -1.");
    io("steep_bright_share", options.steepBrightShare,
       "Share of splotches on slopes steeper than the steep slope that are bright, read from the "
       "terrain's heights; unset keeps the bright share. Bright splotches are found mostly on "
       "such slopes, but the source gives no share.",
       "", UnitInterval());
    io("steep_slope_deg", options.steepSlopeDegrees, "Slope in degrees from which ground is steep.",
       "deg", Inside(0.0, 90.0));
    io("softness", options.softness, "Share of a splotch's radius over which its edge fades.", "",
       AboveUpTo(0.0, 1.0));
    io("raggedness", options.raggedness,
       "Share of a splotch's radius that lobes of its edge pull in by; 0 keeps it round.", "",
       FromBelow(0.0, 1.0));
    FadePixelFields(io, options.fade,
                    "How splotches too small to resolve at the sample spacing fade out.");
    io("swarms", options.swarms, "Swarms of splotches around new craters; off by default.");
}

template <typename IO> void Fields(IO& io, BlastZone& zone) {
    io("x", zone.x, "Surface x of the lander.");
    io("y", zone.y, "Surface y of the lander.");
    io("area", zone.area,
       "Area of the blast zone in m^2; measured: Apollo 18,725 to 53,872 (mean 28,785), Luna 987 "
       "to 3355 (mean 2100), Surveyor 131 to 279 (mean 215), Chang'e-3 2530.",
       "m^2", Above(0.0));
    io("elongation", zone.elongation,
       "Long over short axis of the zone; Apollo about 1.14, Chang'e-3 about 1.74.", "",
       AtLeast(1.0));
    io("heading_deg", zone.headingDegrees,
       "Direction of the long axis, degrees counter-clockwise from east, such as along the "
       "final approach.",
       "deg");
    io("change", zone.change,
       "Mean reflectance change over the zone relative to undisturbed ground at 30 degrees phase; "
       "measured +3.4% to +13.2%, Apollo mean +10.9%.",
       "", Above(-1.0));
    io("halo_change", zone.haloChange,
       "Reflectance change of the fainter halo beyond the zone; +4% at Chang'e-3.", "",
       Above(-1.0));
    io("halo_reach", zone.haloReach,
       "How far the halo reaches over the zone's own reach; 1 for no halo.", "", AtLeast(1.0));
    io("core_radius_m", zone.coreRadius,
       "Radius in meters of the darker core beneath the engine and around the crew's tracks; 0 "
       "for none, a few meters at the Apollo sites.",
       "m", AtLeast(0.0));
    io("core_change", zone.coreChange,
       "Reflectance change of the dark core; -15% to -30% at the Apollo sites.", "", Above(-1.0));
    io("raggedness", zone.raggedness,
       "Share of the zone's radius that lobes of its edge pull in by.", "", FromBelow(0.0, 1.0));
    io("seed", zone.seed, "Varies the edge's lobes.");
}

template <typename IO> void Fields(IO& io, RockAlbedoOptions& options) {
    io("seed", options.seed, "Seed of the per-sample draws of rock too small to resolve.");
    io("reflectance_ratio", options.reflectanceRatio,
       "Rock reflectance over the regolith's; 2.5 after Marshal et al. (2023), within Holt's "
       "(1981) Apollo 16 rocks at 18 to 51% against soils of 14 to 15%; earlier models used 1.5.",
       "", Above(0.0));
    io("unresolved", options.unresolved,
       "Brightens each sample by the share of it covered by boulders too small to resolve, drawn "
       "about the expected share with the scatter of randomly placed rocks: bright speckle.");
}

/// The fields of a synthetic albedo's base color, its noise stacks given apart.
template <typename IO> void SyntheticAlbedoColorFields(IO& io, SyntheticAlbedoOptions& options) {
    io("rgb", options.rgb,
       "Gives three bands, red, green and blue, from the color; off gives one band of the value "
       "alone, ignoring hue and saturation.");
    io("hue_deg", options.hueDegrees,
       "Hue of the base color in degrees, wrapping around; used with rgb only.", "deg");
    io("saturation", options.saturation,
       "Saturation of the base color, 0 for gray; used with rgb only.", "", UnitInterval());
    io("value", options.value,
       "Reflectance of the brightest band; the one band's value without rgb. Higher values give a "
       "brighter surface.",
       "", UnitInterval());
}

template <typename IO> void Fields(IO& io, SlopeAlbedoOptions& options) {
    io("onset_deg", options.onsetDegrees,
       "Slope in degrees from which ground freshens; 20, where Lucey et al. (2014) and Ikeda et "
       "al. (2022) find brightening begin.",
       "deg", FromBelow(0.0, 90.0));
    io("maturity_per_degree", options.maturityPerDegree,
       "Optical maturity (OMAT) gained per degree beyond the onset; about 0.0065 on the crater "
       "walls of Ikeda et al. (2022).",
       "", AtLeast(0.0));
    io("background_maturity", options.backgroundMaturity,
       "OMAT of gentler ground; about 0.10 at 20 degrees in Laue (Ikeda et al. 2022).", "",
       Above(0.0));
    io("max_maturity", options.maxMaturity, "Cap on OMAT; at least the background's.", "",
       Above(0.0));
    io("baseline_m", options.baseline,
       "Meters slopes are measured over, the spacing of the lattice they are read on; 10, the "
       "DTM Ikeda et al. (2022) measured slopes on.",
       "m", Above(0.0));
    io("origin_reflectance", options.originReflectance,
       "Reflectance of the optical maturity origin, from which reflectance scales with maturity "
       "at fixed iron content.");
}

template <typename IO> void Fields(IO& io, HapkeParameters& hapke) {
    io("albedo", hapke.albedo, "Single-scattering albedo w.", "", UnitInterval());
    io("b", hapke.b, "Asymmetry b of the two-term Henyey-Greenstein phase function.", "",
       UnitInterval());
    io("c", hapke.c, "Backscatter fraction c of the two-term Henyey-Greenstein phase function.", "",
       UnitInterval());
    io("roughness_deg", hapke.roughness, "Macroscopic roughness theta-bar, in degrees.", "deg",
       FromBelow(0.0, 90.0));
}

}  // namespace terraingen
