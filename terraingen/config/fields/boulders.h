/// @file
/// @brief The JSON fields of boulder layers, boulder sizes and boulder shapes.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/maps.h"

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

template <typename IO> void Fields(IO& io, BoulderFillet& fillet) {
    io("height_share", fillet.heightShare,
       "Height of the regolith piled against a boulder, as a share of its exposed height, from 0 "
       "to 1; 0 draws no fillets.");
    io("width_ratio", fillet.widthRatio,
       "How far the fillet reaches beyond the boulder's edge, over its diameter; it falls to "
       "nothing there.");
}

template <typename IO> void Fields(IO& io, BoulderVentifacts& ventifacts) {
    io("share", ventifacts.share,
       "Share of boulders with a windward facet planed by sand-laden wind, from 0 to 1; 0 for "
       "none. Gale crater: 0.39 (Bridges et al. 2014).");
    io("wind_azimuth", ventifacts.windAzimuth,
       "Where the abrading wind blows toward, in radians counter-clockwise from east; facets face "
       "the other way.",
       "rad");
    io("min_angle", ventifacts.minAngle,
       "Gentlest facet slope from horizontal, in radians; 50 to 60 deg on basalt (Laity and "
       "Bridges 2009).",
       "rad");
    io("max_angle", ventifacts.maxAngle, "Steepest facet slope from horizontal, in radians.",
       "rad");
    io("second_wind_azimuth", ventifacts.secondWindAzimuth,
       "A second abrading wind, where it blows toward, in radians; some faceted boulders get a "
       "second facet facing it and a keel between. Unset for none.",
       "rad");
    io("keel_share", ventifacts.keelShare,
       "Share of faceted boulders with the second facet, from 0 to 1. Gale crater: 12 of 39 "
       "(Bridges et al. 2014).");
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
    io("max_diameter", models.maxDiameter,
       "Boulders this many metres across or wider are not models, keeping a band of sizes as "
       "models; unset applies no upper limit.",
       "m");
    io("max_samples", models.maxSamples,
       "Boulders spanning this many samples or more are not models; unset applies no such limit.");
}

template <typename IO> void Fields(IO& io, BoulderReliefRange& range) {
    io("min_diameter", range.minDiameter,
       "Smallest boulder drawn into the heights, in metres; smaller ones stay in the lists only, "
       "for the renderer's surface detail.",
       "m");
    io("max_diameter", range.maxDiameter,
       "Boulders this many metres across or wider are not drawn into the heights; unset applies no "
       "upper limit.",
       "m");
    io("models", range.models,
       "Draws the 3D model boulders into the heights as well, as a stand-in for far views.");
}

template <typename IO> void Fields(IO& io, BoulderClusters& clusters) {
    io("share", clusters.share,
       "Share of the boulders between craters gathered in clusters, from 0 to 1; 0 leaves them "
       "scattered.");
    io("spacing", clusters.spacing,
       "Typical distance between cluster centres, in metres; with thomas, one centre per spacing "
       "squared on average.",
       "m");
    io("radius", clusters.radius,
       "Gaussian radius of a cluster, in metres; below the spacing with nearest, at most twice it "
       "with thomas.",
       "m");
    io("model", clusters.model, "How cluster centres are laid out and their clusters combine.");
}

template <typename IO> void Fields(IO& io, BoulderFragmentation& fragmentation) {
    io("min_diameter", fragmentation.minDiameter, "Smallest boulder that breaks, in metres.", "m");
    io("split_wear", fragmentation.splitWear,
       "Wear from 0 (fresh) to 1 from which boulders split into pieces that still fit; 1 breaks "
       "nothing.");
    io("rubble_wear", fragmentation.rubbleWear,
       "Wear from which the pieces scatter as rubble, turn and sink; at least split_wear.");
    io("min_pieces", fragmentation.minPieces, "Fewest pieces a boulder breaks into, at least 2.");
    io("max_pieces", fragmentation.maxPieces, "Most pieces a boulder breaks into, at most 12.");
    io("gap_ratio", fragmentation.gapRatio,
       "Widest crack between split pieces, as a share of the boulder's diameter.");
    io("spread_ratio", fragmentation.spreadRatio,
       "How far rubble scatters beyond the cracks, as a share of the boulder's diameter.");
    io("slab_share", fragmentation.slabShare,
       "Share of broken boulders split by roughly parallel cracks right through them; the rest "
       "crack out from the middle.");
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
    io("edge_softness", style.edgeSoftness,
       "Range of how far edges and corners are worn back where faces meet, as weathering rounds "
       "them, in long-axis lengths from 0 to 0.5; 0 keeps them sharp.");
    io("face_roughness", style.faceRoughness,
       "How far fracture faces undulate, as a share of each plane's offset from 0 to 0.5; 0 keeps "
       "them flat.");
}

template <typename IO> void Fields(IO& io, BoulderShapeOptions& shapes) {
    io("styles", shapes.styles,
       "Kinds of boulder each shape is drawn from, picked by weight; at least one.");
    io("subdivisions", shapes.subdivisions,
       "Times the solid's triangles are split in four, 0 to 6; more give finer, smoother shapes "
       "with more triangles.");
    io("weathering", shapes.weathering,
       "Scales every style's edge softness: 0 gives sharp edges, 1 the styles' own, more is more "
       "worn.");
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
    BoulderSizes& sizes = boulders.sizes;
    BoulderBedrock& bedrock = boulders.bedrock;
    BoulderPose& pose = boulders.pose;
    BoulderRendering& rendering = boulders.rendering;
    io("coverage", sizes.coverage,
       "Golombek-Rapp k, the fraction of the ground covered by boulders, between 0 and 1; used "
       "with golombek_rapp sizes, where higher values give more boulders.");
    io("qa", sizes.qConstant,
       "Constant term of the Golombek-Rapp q = qa + qb / k, per metre; a larger q leaves fewer "
       "large boulders. Also used for golombek_rapp ejecta.",
       "1/m");
    io("qb", sizes.qCoverageCoefficient,
       "Term of the Golombek-Rapp q = qa + qb / k divided by the coverage k, per metre.", "1/m");
    io("min_diameter", sizes.minDiameter,
       "Smallest boulder generated, in metres; lower values add many more small boulders.", "m");
    io("max_diameter", sizes.maxDiameter,
       "Largest boulder generated, in metres, exclusive; above the smallest.", "m");
    io("sizes", spec.sizes,
       "Size distribution of the boulders between craters, per m^2; patches and bedrock scale its "
       "density.");
    io("seed", boulders.seed,
       "Seed of the boulders, their attributes and the patches; changing it gives a different "
       "boulder field of the same statistics.");
    io("patches", boulders.patches,
       "Patches of richer and poorer boulder cover across the ground.");
    io("clusters", boulders.clusters,
       "Clusters of the boulders between craters around scattered centres; off unless share is "
       "set.");
    io("ejecta", boulders.ejecta,
       "Blocks thrown out by craters of the nearest crater layer before this one that dug into "
       "boulder, densest at the rim and thinning outward.");
    io("ejecta_sizes", spec.ejectaSizes,
       "Size distribution of the blocks at the rim of a fresh crater, per m^2; golombek_rapp uses "
       "the ejecta's coverage with qa and qb.");
    io("extrapolate", spec.extrapolate,
       "Lets the size distributions be evaluated outside their valid diameters, such as beyond a "
       "table's published ones; otherwise they must cover the boulder sizes.");
    io("exposure", bedrock.exposure,
       "Raises the abundance of boulders where the ground beneath is bedrock, judged from its "
       "shape; unset disables it.");
    io("bedrock_abundance", bedrock.abundance,
       "Boulder abundance on bedrock over that on soil, and in between in proportion; used with "
       "the exposure field.");
    io("exposure_spacing", bedrock.exposureSpacing,
       "Spacing in metres the ground is read at for the exposure field, fixed so that which "
       "boulders exist does not depend on the level of detail.",
       "m");
    io("bedrock_mean_size", bedrock.meanSize,
       "With a density map, the side in metres of the squares bedrock is held to the map's mean "
       "over, so it gathers boulders without adding any; a GeoTIFF map uses its own cells.",
       "m");
    io("min_height_ratio", pose.minHeightRatio,
       "Smallest boulder height over diameter; each boulder's is drawn uniformly between the two "
       "ratios.");
    io("max_height_ratio", pose.maxHeightRatio,
       "Largest boulder height over diameter; at least the smallest.");
    io("min_burial", pose.minBurial,
       "Smallest buried share of a boulder's height; each boulder's is drawn uniformly between the "
       "two shares.");
    io("max_burial", pose.maxBurial,
       "Largest buried share of a boulder's height, below 1; more burial leaves less of the "
       "boulder exposed.");
    io("max_lean", pose.maxLean, "Largest tilt of a boulder from the vertical, in radians.", "rad");
    io("side_share", pose.sideShare,
       "Share of boulders resting on a side, their intermediate axis up, rather than on their "
       "broadest face.");
    io("end_share", pose.endShare,
       "Share of boulders standing on an end, their long axis up; with side_share, at most 1.");
    io("max_tilt", pose.maxTilt,
       "Largest tilt of a boulder's up axis from the vertical in a random direction, after its "
       "lean, in radians; tilts are spread evenly over that cap of directions.",
       "rad");
    io("follow_slope", pose.followSlope,
       "Settles each boulder on the slope of the ground beneath it before its tilt.");
    io("max_slope", pose.maxSlope,
       "Steepest ground, across a boulder's diameter, that boulders stay on, in radians; steeper "
       "ground has none, models and drawn boulders alike. Unset keeps boulders on any slope.",
       "rad");
    io("burial_by_wear", pose.burialByWear,
       "How far wear sets burial, from 0 to 1: blocks of fresh craters sit near the smallest "
       "burial and those of worn ones sink toward the largest, while boulders between craters "
       "span every stage; 0 draws burial uniformly.");
    io("fillet", boulders.fillet,
       "Regolith piled against each boulder's base, around relief and models alike; off unless "
       "height_share is set.");
    io("ventifacts", boulders.ventifacts,
       "Windward facets planed on boulders by sand-laden wind, from wear; off unless share is "
       "set.");
    io("fragmentation", boulders.fragmentation,
       "Boulders broken by wear into pieces that replace them, split and then scattered as "
       "rubble; off unless split_wear is below 1.");
    io("relief", rendering.relief, "How the boulders drawn into the heights look there.");
    io("models", rendering.models,
       "Which boulders become 3D models of their own rather than relief in the heights; neither "
       "threshold set means no models.");
    io("relief_range", rendering.reliefRange,
       "Which boulders are drawn into the heights by diameter: none below min_diameter or from "
       "max_diameter, and models only when asked.");
    io("shapes", rendering.shapes,
       "The boulders' 3D boulder shapes, used by shape relief and by models.");
    io("shape_files", spec.shapeFiles,
       "Rock meshes (.obj with its .mtl, .gltf or .glb) the boulder shapes are drawn from instead "
       "of the procedural styles, each boulder picking one by its shape seed; paths relative to "
       "the configuration. Empty keeps the procedural shapes.");
    io("density_map", spec.densityMap,
       "Map that sets the abundance of boulders between craters point by point, scaling the "
       "sizes to what it measures; unset keeps the abundance the sizes set everywhere.");
    io("manual", spec.manual,
       "Catalog boulders placed as given and never thinned or left out; x, y and diameter are "
       "read.");
    io("completeness", spec.completeness,
       "Where the manual boulders are a complete catalog: the diameter in metres from which they "
       "stand in for generated boulders, infinite or no value where there is no catalog. Unset "
       "keeps every generated boulder.");
    io("shape_prototypes", rendering.shapePrototypes,
       "How many shapes the boulders share, so they can be drawn as instances; 0 gives every "
       "boulder a shape of its own.");
    io("fade", boulders.fade, "How boulders too small to resolve at the sample spacing fade out.");
}

}  // namespace terraingen::config
