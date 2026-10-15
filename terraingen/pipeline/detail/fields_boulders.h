/// @file
/// @brief Internal: the JSON fields of boulder layers, boulder sizes and boulder shapes.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/detail/fields_common.h"

#include <string_view>

namespace terraingen::config {

/// Description of the curve of an SP-8023 boulder size model.
inline constexpr std::string_view kCurveHelp =
    "Which of the model's minimum, nominal and maximum curves to use, from fewest to most "
    "boulders.";

template <> struct SpecName<GolombekRappBouldersSpec> {
    static constexpr std::string_view kName = "golombek_rapp";
    static constexpr std::string_view kDescription =
        "Golombek and Rapp boulder sizes, set by the fraction of ground the boulders cover and the "
        "layer's qa and qb.";
};
template <typename IO> void Fields(IO& /*io*/, GolombekRappBouldersSpec& /*spec*/) {}

template <> struct SpecName<Sp8023IntercraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_intercrater";
    static constexpr std::string_view kDescription =
        "Blocks between craters from NASA SP-8023, the lunar surface design model (Figure 7).";
};
template <typename IO> void Fields(IO& io, Sp8023IntercraterBouldersSpec& spec) {
    io("curve", spec.curve, kCurveHelp);
}

template <> struct SpecName<Sp8023CraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_crater";
    static constexpr std::string_view kDescription =
        "Blocks around blocky craters from NASA SP-8023 (Figure 9), for the ejecta of craters "
        "that reached bedrock.";
};
template <typename IO> void Fields(IO& io, Sp8023CraterBouldersSpec& spec) {
    io("curve", spec.curve, kCurveHelp);
}

template <> struct SpecName<SynthterrainBouldersSpec> {
    static constexpr std::string_view kName = "synthterrain";
    static constexpr std::string_view kDescription =
        "One of the power-law boulder distributions synthterrain offers, for comparison with it.";
};
template <typename IO> void Fields(IO& io, SynthterrainBouldersSpec& spec) {
    io("model", spec.model, "Which of synthterrain's power-law boulder distributions to use.");
}

template <typename IO> void Fields(IO& io, BoulderPatches& patches) {
    io("spread", patches.spread,
       "Boulder abundance varies from 1 / spread to spread times its mean, at least 1; 1 is the "
       "same everywhere.");
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

template <> struct SpecName<BoulderLayerSpec> {
    static constexpr std::string_view kName = "boulders";
    static constexpr std::string_view kDescription =
        "Boulders scattered over the ground from a size distribution, in patches and around fresh "
        "craters, as relief or 3D models.";
};
template <typename IO> void Fields(IO& io, BoulderLayerSpec& spec) {
    BoulderLayerOptions& boulders = spec.boulders;
    io("coverage", boulders.coverage,
       "Golombek-Rapp k, the fraction of the ground covered by boulders, between 0 and 1; used "
       "with golombek_rapp sizes, where higher values give more boulders.");
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
       "boulder field of the same statistics.");
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
       "shape; unset disables it.");
    io("bedrock_abundance", boulders.bedrockAbundance,
       "Boulder abundance on bedrock over that on soil, and in between in proportion; used with "
       "the exposure field.");
    io("exposure_spacing", boulders.exposureSpacing,
       "Spacing in metres the ground is read at for the exposure field, fixed so that which "
       "boulders exist does not depend on the level of detail.",
       "m");
    io("min_height_ratio", boulders.minHeightRatio,
       "Smallest boulder height over diameter; each boulder's is drawn uniformly between the two "
       "ratios.");
    io("max_height_ratio", boulders.maxHeightRatio,
       "Largest boulder height over diameter; at least the smallest.");
    io("min_burial", boulders.minBurial,
       "Smallest buried share of a boulder's height; each boulder's is drawn uniformly between the "
       "two shares.");
    io("max_burial", boulders.maxBurial,
       "Largest buried share of a boulder's height, below 1; more burial leaves less of the "
       "boulder exposed.");
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
       "boulder a shape of its own.");
    io("fade", boulders.fade, "How boulders too small to resolve at the sample spacing fade out.");
}

}  // namespace terraingen::config
