/// @file
/// @brief The JSON fields of the albedo, its sources and its crater layers.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

#include <string_view>

namespace terraingen::config {

template <> struct SpecName<SyntheticAlbedoSpec> {
    static constexpr std::string_view kName = "synthetic";
    static constexpr std::string_view kDescription =
        "A base colour in hue, saturation and value, each varied by noise: an albedo for terrain "
        "without data.";
};
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

template <> struct SpecName<RasterAlbedoSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "Albedo read from one or three bands of a GeoTIFF in the terrain's projection, such as an "
        "orbital albedo map.";
};
template <typename IO> void Fields(IO& io, RasterAlbedoSpec& spec) {
    io("path", spec.path,
       InputPathHelp("GeoTIFF albedo map in the terrain's projected coordinates"));
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

template <typename IO> void Fields(IO& io, CraterAlbedoTexture& texture) {
    io("streaks", texture.streaks,
       "Share of the excess radial streaks take away between filaments, from 0 to 1, breaking "
       "ejecta, rays and interiors into fine streaks; 0 leaves them smooth.");
    io("streak_width_degrees", texture.streakWidthDegrees,
       "Typical width of a filament around the crater, in degrees.", "deg");
    io("streak_length_radii", texture.streakLengthRadii,
       "Typical length of a filament outward from the crater, in crater radii.");
    io("continuous_radii", texture.continuousRadii,
       "How far beyond the rim the ejecta stay continuous before breaking into filaments, in "
       "crater radii; 0 streaks them from the rim.");
    io("lobes", texture.lobes,
       "How far the ejecta's edge wanders in and out, as a share of its reach from 0 to 0.9; 0 "
       "keeps it circular.");
    io("lobe_count", texture.lobeCount, "Typical number of lobes around a crater, 1 to 64.");
    io("floor_share", texture.floorShare,
       "Interior excess on the floor over that at the rim, from 0 to 1; 1 keeps the interior "
       "even, lower values brighten the walls over the floor.");
    io("seed", texture.seed, "Varies every crater's texture.");
}

template <typename IO> void Fields(IO& io, ReflectanceZone& zone) {
    io("share", zone.share, "Share of craters, from 0 to 1, with the zone.");
    io("change", zone.change,
       "Reflectance change relative to the pre-impact surface, drawn per crater; above -1, "
       "negative values darken.");
    io("coefficient", zone.coefficient,
       "Coefficient of the zone's reach from the crater's centre, coefficient D^exponent metres "
       "for diameter D in metres; positive.",
       "m");
    io("exponent", zone.exponent, "Exponent of the diameter in the zone's reach; positive.");
    io("scale", zone.scale,
       "Factor on the reach, drawn per crater; positive and bounded, a wider distribution gives "
       "more varied zones.");
    io("raggedness", zone.raggedness,
       "Share of the reach, from 0 up to 1, that fingers of the edge pull in by; 0 keeps the "
       "zone round, higher values make it ray-like.");
    io("finger_width_degrees", zone.fingerWidthDegrees,
       "Typical width of the edge's fingers around the crater, in degrees.", "deg");
    io("feather", zone.feather,
       "Share of the reach, from 0 to 1 excluding 0, over which the zone fades out at its edge.");
    io("retention_myr", zone.retentionMyr,
       "How long the zone lasts, in Myr, fading like the bright material; unset lasts as long as "
       "the bright material.",
       "Myr");
}

template <typename IO> void Fields(IO& io, ZoneShadows& shadows) {
    io("enabled", shadows.enabled,
       "Gives the distal zones shadows behind rises such as crater rims, read from the terrain's "
       "heights; off by default.");
    io("jet_angle_degrees", shadows.jetAngleDegrees,
       "Steepest angle in degrees, between 0 and 90 excluded, at which the jet forming the distal "
       "zones can drop behind a rise; lower values give longer shadows. Illustrative.",
       "deg");
    io("depth", shadows.depth,
       "Depth in metres of the ground below the jet at which the shadow is full; positive. "
       "Illustrative.",
       "m");
    io("azimuths", shadows.azimuths, "Rays around a crater the ground is sampled on, 8 to 8192.");
    io("radial_samples", shadows.radialSamples,
       "Samples along each ray out to the distal zones' reach, 8 to 8192.");
}

template <typename IO> void Fields(IO& io, CraterReflectanceZones& zones) {
    io("enabled", zones.enabled,
       "Gives new small craters the reflectance zones of LROC temporal pairs; off by default.");
    io("max_diameter", zones.maxDiameter,
       "Largest crater diameter, in metres, with zones; the published fits stop near 43 m.", "m");
    io("mature_depth", zones.matureDepth,
       "Depth in metres of mature regolith; only craters excavating deeper get the proximal "
       "high reflectance zone.",
       "m");
    io("excavation_ratio", zones.excavationRatio,
       "Excavation depth over crater diameter; positive.");
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
    CraterSelection& selection = options.selection;
    io("min_diameter", selection.minDiameter,
       "Smallest diameter, in metres, of craters given bright material.", "m");
    OptionalBound(io, "max_diameter", selection.maxDiameter,
                  "Largest diameter, in metres, of craters given bright material; unset has no "
                  "upper limit.",
                  "m");
    io("min_age_gyr", selection.minAgeGyr,
       "Youngest age, in Gyr, of craters given bright material.", "Gyr");
    OptionalBound(io, "max_age_gyr", selection.maxAgeGyr,
                  "Oldest age, in Gyr, of craters given bright material; unset has no upper limit.",
                  "Gyr");
    io("manual", selection.manual,
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
    io("texture", options.texture,
       "Filaments, lobed ejecta edges and walls brighter than floors; off unless streaks, lobes "
       "or a floor share below 1 are set.");
    io("zones", options.zones,
       "Bright and dark reflectance zones around new small craters, after Robinson et al. "
       "(2015) and Speyerer et al. (2016); off by default.");
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

template <> struct SpecName<CraterAlbedoSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Bright ejecta, interiors and rays of the craters of a crater layer, fading with crater "
        "age.";
};
template <typename IO> void Fields(IO& io, CraterAlbedoSpec& spec) {
    io("crater_layer", spec.craterLayer,
       "Which top-level crater layer's craters to brighten, counting from 0; unset uses every "
       "top-level crater layer.");
    Fields(io, spec.options);
}

template <> struct SpecName<SplotchAlbedoSpec> {
    static constexpr std::string_view kName = "splotches";
    static constexpr std::string_view kDescription =
        "Dark and bright splotches that secondary impacts of loose regolith leave over the whole "
        "surface, after Speyerer et al. (2016).";
};
template <typename IO> void Fields(IO& io, SplotchAlbedoSpec& spec) {
    SplotchOptions& options = spec.options;
    io("seed", options.seed, "Seed of where splotches fall and what they are.");
    io("rate_coefficient", options.rateCoefficient,
       "Coefficient a of the cumulative formation rate a d^-b per km^2 per year, d the diameter "
       "in km; positive, higher values give more splotches.");
    io("rate_exponent", options.rateExponent,
       "Slope b of the cumulative formation rate; positive, higher values give relatively more "
       "small splotches.");
    io("lifetime_myr", options.lifetimeMyr,
       "How long a splotch lasts in Myr, fading linearly; positive, the default is the time "
       "splotches take to churn the top centimetres.",
       "Myr");
    io("min_diameter", options.minDiameter,
       "Smallest splotch diameter in metres; the published fit is extrapolated below 10 m.", "m");
    io("max_diameter", options.maxDiameter,
       "Largest splotch diameter in metres, above the smallest.", "m");
    io("dark_share", options.darkShare, "Share of splotches, from 0 to 1, darker than the ground.");
    io("bright_share", options.brightShare,
       "Share of splotches, from 0 to 1, brighter than the ground; the rest are mixed, dark on "
       "one side and bright on the other.");
    io("dark_change", options.darkChange,
       "Reflectance change of a dark splotch relative to the ground before it, drawn per "
       "splotch; above -1.");
    io("bright_change", options.brightChange,
       "Reflectance change of a bright splotch, drawn per splotch; above -1.");
    io("steep_bright_share", options.steepBrightShare,
       "Share of splotches on slopes steeper than the steep slope that are bright, from 0 to 1, "
       "read from the terrain's heights; unset keeps the bright share. Bright splotches are "
       "found mostly on such slopes, but the source gives no share.");
    io("steep_slope_degrees", options.steepSlopeDegrees,
       "Slope in degrees, between 0 and 90 excluded, from which ground is steep.", "deg");
    io("softness", options.softness,
       "Share of a splotch's radius, from 0 to 1 excluding 0, over which its edge fades.");
    io("raggedness", options.raggedness,
       "Share of a splotch's radius, from 0 up to 1, that lobes of its edge pull in by; 0 keeps "
       "it round.");
    io("fade", options.fade, "How splotches too small to resolve at the sample spacing fade out.");
}

template <typename IO> void Fields(IO& io, AlbedoSpec& spec) {
    io("source", spec.source,
       "Base albedo of the terrain, before its layers: a synthetic colour or a raster map.");
    io("layers", spec.layers,
       "Layers applied to the source's albedo in order, such as bright crater ejecta and rays.");
}

}  // namespace terraingen::config
