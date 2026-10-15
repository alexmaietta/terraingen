/// @file
/// @brief Internal: the JSON fields of the albedo, its sources and its crater layers.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/detail/fields_common.h"

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

template <typename IO> void Fields(IO& io, CraterAlbedoOptions& options) {
    io("seed", options.seed,
       "Seed of which craters get bright material, their rays and their impacts; changing it "
       "gives a different pattern of the same statistics.");
    io("min_diameter", options.minDiameter,
       "Smallest diameter, in metres, of craters given bright material.", "m");
    OptionalBound(io, "max_diameter", options.maxDiameter,
                  "Largest diameter, in metres, of craters given bright material; unset has no "
                  "upper limit.",
                  "m");
    io("min_age_gyr", options.minAgeGyr, "Youngest age, in Gyr, of craters given bright material.",
       "Gyr");
    OptionalBound(io, "max_age_gyr", options.maxAgeGyr,
                  "Oldest age, in Gyr, of craters given bright material; unset has no upper limit.",
                  "Gyr");
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

template <typename IO> void Fields(IO& io, AlbedoSpec& spec) {
    io("source", spec.source,
       "Base albedo of the terrain, before its layers: a synthetic colour or a raster map.");
    io("layers", spec.layers,
       "Layers applied to the source's albedo in order, such as bright crater ejecta and rays.");
}

}  // namespace terraingen::config
