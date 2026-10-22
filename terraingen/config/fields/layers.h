/// @file
/// @brief The JSON fields of the noise, roughness, bedform, transform and region layers.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/sources.h"

#include <string>
#include <string_view>

namespace terraingen::config {

template <typename IO> void Fields(IO& io, NoiseLayerConfig& config) {
    NoiseLayer& layer = config.layer;
    FractalParams& fractal = layer.fractal;
    io("enabled", layer.enabled, "Includes this layer in the stack; disabled layers are skipped.");
    io("basis", fractal.basis, "Single-octave noise the fractal is built from.");
    io("fractal", fractal.type, "How the octaves of the basis noise are combined.");
    io("blend", layer.blend,
       "How this layer combines with the height of the layers before it; the first enabled layer "
       "sets the height instead.");
    io("seed", fractal.seed,
       "Seed of the noise; changing it gives a different pattern of the same character.");
    io("wavelength", fractal.wavelength,
       "Wavelength of the first, broadest octave, in metres; sets the size of the largest "
       "features.",
       "m", Above(0.0));
    io("amplitude", config.amplitude,
       "Largest value of the layer above or below 0: in metres for terrain noise, where unset is "
       "0.1 times the wavelength, near the lunar highland median slope at any wavelength; unset "
       "is 100 for albedo noise.",
       "m");
    io("octaves", fractal.octaves,
       "Number of octaves, at least 1; each one adds detail at a shorter wavelength.", "",
       Between(1.0, 32.0));
    io("lacunarity", fractal.lacunarity,
       "Frequency ratio between successive octaves; greater than 1.", "", Above(1.0));
    io("gain", fractal.gain,
       "Weight ratio between successive octaves, greater than 0; higher values give rougher "
       "terrain. Unused when the Hurst exponent is set.",
       "", Above(0.0));
    io("hurst", config.hurst,
       "Hurst exponent that sets the octave gain to lacunarity^-hurst: near 1 is smooth, lower is "
       "rougher at short baselines. Unset uses the gain.");
    io("offset", fractal.offset,
       "Ridge offset of the ridged and hybrid multifractal types; greater than 0.", "", Above(0.0));
    io("warp_amplitude", layer.warpAmplitude,
       "Domain warp displacement in metres, which bends the noise into swirled shapes; 0 "
       "disables warping.",
       "m", AtLeast(0.0));
    io("warp_wavelength", layer.warpWavelength, "Wavelength of the warp field, in metres.", "m",
       Above(0.0));
}

template <> struct SpecName<NoiseLayerSpec> {
    static constexpr std::string_view kName = "noise";
    static constexpr std::string_view kDescription =
        "A stack of fractal noise layers added to the terrain, for hills, ridges and rolling "
        "relief.";
};
template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer) {
    io("stack", layer.stack, NoiseStackHelp(1));
    io("estimate_from_base", layer.estimateFromBase,
       "Continues the base's roughness into the noise: each layer's unset hurst becomes the "
       "Hurst exponent of the base's variogram at its native cell size, and each unset "
       "amplitude gives the layer the RMS height difference that variogram extrapolates to at "
       "a lag of the layer's wavelength. Meant for wavelengths at or below the base's cell "
       "size; needs a base.");
}

template <> struct SpecName<RoughnessLayerSpec> {
    static constexpr std::string_view kName = "roughness";
    static constexpr std::string_view kDescription =
        "Fine roughness from octaves of noise, scaled to a given RMS slope over a baseline.";
};
/// What a roughness's "rms_slope" field means.
inline constexpr std::string_view kRmsSlopeHelp =
    "RMS slope, as rise over run, over the baseline when every octave is resolved; higher values "
    "give rougher ground.";

/// What a roughness's "hurst" field means.
inline constexpr std::string_view kRoughnessHurstHelp =
    "Hurst exponent, in (0, 1]: octave amplitudes grow as wavelength^hurst, so lower values put "
    "relatively more roughness at short wavelengths.";

/// The fields of a roughness's octaves: their wavelengths, seed and fade.
template <typename IO> void RoughnessOctaveFields(IO& io, RoughnessOptions& roughness) {
    io("max_wavelength", roughness.maxWavelength, "Wavelength of the longest octave, in metres.",
       "m", Above(0.0));
    io("min_wavelength", roughness.minWavelength,
       "Wavelength of the shortest octave, in metres; octaves halve from the longest down to it, "
       "spanning at most 40 octaves.",
       "m", Above(0.0));
    io("seed", roughness.seed,
       "Seed of the noise; changing it gives a different pattern of the same roughness.");
    io("fade", roughness.fade,
       "How octaves too fine to resolve at the sample spacing fade out, by samples per "
       "wavelength.");
}

template <typename IO> void Fields(IO& io, RoughnessOptions& roughness) {
    io("rms_slope", roughness.rmsSlope, std::string(kRmsSlopeHelp), "", Above(0.0));
    io("baseline", roughness.baseline, "Baseline the RMS slope is given over, in metres.", "m",
       Above(0.0));
    io("hurst", roughness.hurst, std::string(kRoughnessHurstHelp), "", AboveUpTo(0.0, 1.0));
    RoughnessOctaveFields(io, roughness);
}

template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec) {
    io("rms_slope", spec.rmsSlope,
       std::string(kRmsSlopeHelp) +
           " Unset is 0.15, or with estimate_from_base the base's RMS slope extrapolated to the "
           "baseline.",
       "", Above(0.0));
    io("baseline", spec.roughness.baseline, "Baseline the RMS slope is given over, in metres.", "m",
       Above(0.0));
    io("hurst", spec.hurst,
       std::string(kRoughnessHurstHelp) + " Unset is 0.8, or with estimate_from_base the base's.",
       "", AboveUpTo(0.0, 1.0));
    io("estimate_from_base", spec.estimateFromBase,
       "Continues the base's roughness below its resolution: fits the Hurst exponent and RMS "
       "height difference to the base's variogram at lags of 1 to 8 native cells, over up to "
       "512 cells about its centre, and extrapolates the RMS slope to the baseline. Set "
       "rms_slope or hurst to override an estimate. Needs a base with relief; keep "
       "max_wavelength near the base's cell size.");
    RoughnessOctaveFields(io, spec.roughness);
}

template <typename IO> void Fields(IO& io, DuneRipples& ripples) {
    io("wavelength", ripples.wavelength,
       "Crest-to-crest spacing of the ripples on flat sand, in metres; 0 gives no ripples.", "m",
       AtLeast(0.0));
    io("height", ripples.height, "Trough-to-crest height of the ripples on flat sand, in metres.",
       "m", AtLeast(0.0));
    io("lee_fraction", ripples.leeFraction,
       "Share of a ripple wavelength the lee face takes, in (0, 1): 0.5 is symmetric, lower "
       "values give a steeper lee face.",
       "", Inside(0.0, 1.0));
    io("sinuosity", ripples.sinuosity,
       "How far a ripple crest wanders along its length, in wavelengths.", "", AtLeast(0.0));
    io("slope_coupling", ripples.slopeCoupling,
       "How strongly the dune's slope turns and tightens the ripples: they shorten on steep lee "
       "faces and turn on the flanks; 0 keeps them straight across the wind.",
       "", AtLeast(0.0));
    io("interdune_cover", ripples.interduneCover,
       "Share of the ripple height kept on the ground between dunes, from 0 to 1.", "",
       UnitInterval());
    io("defect_density", ripples.defectDensity,
       "Forked crests (Y-junctions): pairs of crest ends per metre of crest, as measured on dune "
       "fields (Ewing et al. 2010); 0 for none. Mars large ripples: 0.099.",
       "1/m", AtLeast(0.0));
    io("flank_straightening", ripples.flankStraightening,
       "How far crests straighten off the stoss, from 0 to 1: 1 keeps the sinuosity on flat sand "
       "and stoss slopes and takes it away on flanks and lee slopes, as Martian large ripples "
       "do.",
       "", UnitInterval());
    io("fade", ripples.fade,
       "How ripples too fine for the sample spacing fade out, by samples per wavelength.");
}

template <typename IO> void Fields(IO& io, DuneGrainflows& flows) {
    io("cover", flows.cover,
       "Chance, from 0 to 1, that each lattice cell of the slip faces holds a grainflow; 0 for "
       "none.",
       "", UnitInterval());
    io("min_scarp_width", flows.minScarpWidth, "Narrowest scarp, in metres.", "m", Above(0.0));
    io("max_scarp_width", flows.maxScarpWidth, "Widest scarp, in metres.", "m", Above(0.0));
    io("min_scarp_length", flows.minScarpLength, "Shortest scarp downslope, in metres.", "m",
       Above(0.0));
    io("max_scarp_length", flows.maxScarpLength, "Longest scarp downslope, in metres.", "m",
       Above(0.0));
    io("min_lobe_width", flows.minLobeWidth, "Narrowest lobe, in metres.", "m", Above(0.0));
    io("max_lobe_width", flows.maxLobeWidth, "Widest lobe, in metres.", "m", Above(0.0));
    io("min_lobe_length", flows.minLobeLength, "Shortest lobe downslope, in metres.", "m",
       Above(0.0));
    io("max_lobe_length", flows.maxLobeLength, "Longest lobe downslope, in metres.", "m",
       Above(0.0));
    io("thickness", flows.thickness, "Depth of a scarp and height of a lobe, in metres.", "m",
       AtLeast(0.0));
    io("fade", flows.fade,
       "How grainflows too narrow for the sample spacing fade out, by samples across a lobe.");
}

template <> struct SpecName<DuneLayerSpec> {
    static constexpr std::string_view kName = "dunes";
    static constexpr std::string_view kDescription =
        "Wind-blown dunes added to the terrain: isolated barchans or domes, or fields of "
        "transverse, barchanoid or linear crests, with slip faces and optional ripples on them.";
};
/// The fields of every dune's shape and the wind that shapes it.
template <typename IO> void DuneProfileFields(IO& io, DuneOptions& dunes) {
    io("shape", dunes.shape, "The kind of dune.");
    RadianField(io, "wind_azimuth_deg", dunes.windAzimuth,
                WindAzimuthHelp("wind", "slip faces face it"));
    RadianField(io, "azimuth_spread_deg", dunes.azimuthSpread,
                "Largest difference of each barchan or dome's wind from the azimuth, in degrees.",
                AtLeast(0.0));
    RadianField(io, "slip_face_angle_deg", dunes.slipFaceAngle,
                "Slope of the lee slip faces, in degrees: the sand's angle of repose.",
                Inside(0.0, 90.0));
    RadianField(io, "stoss_slope_deg", dunes.stossSlope,
                "Mean slope of the windward faces, in degrees; below the slip face angle.",
                Inside(0.0, 90.0));
    io("stoss_exponent", dunes.stossExponent,
       "Exponent alpha of the cos^alpha windward profile of dunes; higher is blunter.", "",
       Above(0.0));
    io("dome_exponent", dunes.domeExponent, "Exponent alpha of the cos^alpha profile of domes.", "",
       Above(0.0));
}

/// The fields of isolated barchans and domes, one per lattice cell at most.
template <typename IO> void DuneLatticeFields(IO& io, DuneOptions& dunes) {
    io("spacing", dunes.spacing,
       "Barchans and domes: side of the lattice cells, each holding at most one dune, in metres.",
       "m", Above(0.0));
    io("density", dunes.density, "Barchans and domes: chance a cell holds a dune, from 0 to 1.", "",
       UnitInterval());
    io("width", dunes.width, "Barchans and domes: mean width, horn to horn, in metres.", "m",
       Above(0.0));
    io("width_spread", dunes.widthSpread,
       "Barchans and domes: each width varies from (1 - spread) to (1 + spread) times the mean, "
       "spread in [0, 1).",
       "", FromBelow(0.0, 1.0));
    io("min_width", dunes.minWidth,
       "Barchans: width in metres below which a dune is a dome without a slip face, blending "
       "into barchans up to 1.5 times it.",
       "m", AtLeast(0.0));
    io("height_ratio", dunes.heightRatio, "Barchans and domes: height over width.", "", Above(0.0));
    io("horn_length", dunes.hornLength,
       "Barchans: how far the horns reach downwind of the crest, in dune heights.", "",
       AtLeast(0.0));
    io("dome_length", dunes.domeLength, "Domes: length along the wind over width.", "", Above(0.0));
}

/// The fields of fields of transverse, barchanoid and linear crests.
template <typename IO> void DuneCrestFields(IO& io, DuneOptions& dunes) {
    io("wavelength", dunes.wavelength,
       "Transverse, barchanoid and linear dunes: crest-to-crest spacing, in metres.", "m",
       Above(0.0));
    io("height", dunes.height, "Transverse, barchanoid and linear dunes: crest height, in metres.",
       "m", AtLeast(0.0));
    io("sinuosity", dunes.sinuosity,
       "Transverse, barchanoid and linear dunes: how far a crest wanders along its length, in "
       "wavelengths.",
       "", AtLeast(0.0));
    io("defects", dunes.defects,
       "Transverse, barchanoid and linear dunes: share of the height that varies along crests, "
       "lowering and ending some, from 0 to 1.",
       "", UnitInterval());
    io("defect_density", dunes.defectDensity,
       "Transverse, barchanoid and linear dunes: pairs of crest ends per metre of crest, so crests "
       "break into segments about 1 / defect_density long; 0 for none. Olympia Undae: 0.0027 "
       "(Ewing et al. 2010).",
       "1/m", AtLeast(0.0));
    RadianField(io, "flank_slope_deg", dunes.flankSlope,
                "Linear dunes: mean slope of both flanks, in degrees.", Inside(0.0, 90.0));
}

template <typename IO> void Fields(IO& io, DuneOptions& dunes) {
    DuneProfileFields(io, dunes);
    DuneLatticeFields(io, dunes);
    DuneCrestFields(io, dunes);
    io("seed", dunes.seed,
       "Seed of the dunes and ripples; changing it gives a different layout of the same "
       "statistics.");
    AnchorFields(io, dunes.anchor,
                 "the dunes are laid out in; they are true to scale near it and absent on the far "
                 "hemisphere");
    io("fade", dunes.fade,
       "How dunes too fine for the sample spacing fade out, by samples across their width or "
       "wavelength.");
    io("ripples", dunes.ripples, "Ripples superposed on the dunes.");
    io("grainflows", dunes.grainflows,
       "Grainflows on the slip faces: scarps below the brink and lobes below them; off unless "
       "cover is set.");
}
template <typename IO> void Fields(IO& io, DuneLayerSpec& spec) {
    Fields(io, spec.dunes);
}

template <> struct SpecName<PavementLayerSpec> {
    static constexpr std::string_view kName = "pavement";
    static constexpr std::string_view kDescription =
        "Fractured bedrock: polygonal plates, each tilted and stepped, parted by cracks or raised "
        "ridges, split again by finer octaves of cracks; lays bedrock over the material map.";
};
template <typename IO> void Fields(IO& io, PavementOptions& pavement) {
    io("plate_size", pavement.plateSize, "Mean plate width, in metres.", "m", Above(0.0));
    io("jitter", pavement.jitter,
       "How far plate centres stray from a square lattice, from 0 (square plates) to 1.", "",
       UnitInterval());
    io("warp", pavement.warp, "Bend and roughness of the plate edges, in plate sizes.", "",
       AtLeast(0.0));
    io("crack_width", pavement.crackWidth, "Width of the cracks between plates, in metres.", "m",
       Above(0.0));
    io("crack_depth", pavement.crackDepth,
       "Depth of the cracks, or height of the ridges when raised, in metres.", "m", AtLeast(0.0));
    io("raised", pavement.raised, "Whether plates are parted by raised ridges instead of cracks.");
    io("edge_rounding", pavement.edgeRounding,
       "Width of the rounded shoulder beside each crack, in metres.", "m", AtLeast(0.0));
    RadianField(io, "max_tilt_deg", pavement.maxTilt, "Largest tilt of a plate, in degrees.",
                FromBelow(0.0, 90.0));
    io("max_step", pavement.maxStep, "Largest height offset of a plate, up or down, in metres.",
       "m", AtLeast(0.0));
    io("octaves", pavement.octaves,
       "Crack octaves, 1 to 8: each finer octave splits every plate again, its cracks ending at "
       "the plate's edges.",
       "", Between(1.0, 8.0));
    io("lacunarity", pavement.lacunarity, "Plate size ratio between octaves, above 1.", "",
       Above(1.0));
    io("gain", pavement.gain,
       "Ratio of crack width, depth, rounding, tilt and step between octaves, in (0, 1].", "",
       AboveUpTo(0.0, 1.0));
    io("seed", pavement.seed, "Seed of the plates.");
    ExposureFields(io, pavement.exposure, pavement.exposureSpacing,
                   "Shows plates only where the ground beneath is bare bedrock, fading out over "
                   "soil; unset shows them everywhere.");
    AnchorFields(io, pavement.anchor, "the plates are laid out in");
    io("fade", pavement.fade,
       "How plates and cracks too fine for the sample spacing fade out, by samples across them.");
}
template <typename IO> void Fields(IO& io, PavementLayerSpec& spec) {
    Fields(io, spec.pavement);
}

template <> struct SpecName<StrataLayerSpec> {
    static constexpr std::string_view kName = "strata";
    static constexpr std::string_view kDescription =
        "Layered rock: the ground beneath terraced into dipping beds, each a flat bench below a "
        "cliff, so hills become stepped buttes and mesas; lays bedrock over the cliffs.";
};
template <typename IO> void Fields(IO& io, StrataOptions& strata) {
    io("bed_thickness", strata.bedThickness, "Mean thickness of a bed, in metres.", "m",
       Above(0.0));
    io("thickness_variation", strata.thicknessVariation,
       "How far each bed boundary strays from even spacing, in bed thicknesses, in [0, 1).", "",
       FromBelow(0.0, 1.0));
    RadianField(io, "dip_deg", strata.dip, "Dip of the beds from horizontal, in degrees, below 90.",
                FromBelow(0.0, 90.0));
    RadianField(io, "dip_azimuth_deg", strata.dipAzimuth,
                "Direction the beds dip toward, in degrees counter-clockwise from +x (local east "
                "on a body).");
    io("sharpness", strata.sharpness,
       "How sharply each bed is cut into a flat bench and a cliff, at least 1; 1 leaves the "
       "ground as it is.",
       "", AtLeast(1.0));
    io("strength", strata.strength, "Share of the terracing applied, in [0, 1].", "",
       UnitInterval());
    io("cliff_share", strata.cliffShare,
       "Share of each bed's height, at its top, laid as bedrock in the material map, in (0, 1].",
       "", AboveUpTo(0.0, 1.0));
    io("seed", strata.seed, "Seed of the bed boundaries.");
    AnchorFields(io, strata.anchor, "the dip is measured in");
    io("hard_share", strata.hardShare,
       "Share of the beds that are hard rock, from 0 to 1: hard beds form benches and cliffs of "
       "bedrock, soft beds slopes cut at soft_sharpness without bedrock.",
       "", UnitInterval());
    io("soft_sharpness", strata.softSharpness,
       "How sharply soft beds are cut into benches and slopes, at least 1; 1 leaves them as they "
       "are.",
       "", AtLeast(1.0));
}
template <typename IO> void Fields(IO& io, StrataLayerSpec& spec) {
    Fields(io, spec.strata);
}

template <> struct SpecName<SandShadowLayerSpec> {
    static constexpr std::string_view kName = "sand_shadow";
    static constexpr std::string_view kDescription =
        "Sand shadows: a tapering sand tail in the lee of each boulder of the nearest boulder "
        "layer before this one; lays sand over the material map.";
};
template <typename IO> void Fields(IO& io, SandShadowOptions& shadows) {
    RadianField(io, "wind_azimuth_deg", shadows.windAzimuth,
                WindAzimuthHelp("wind", "tails trail away from boulders along it"));
    io("length_ratio", shadows.lengthRatio,
       "Length of a tail beyond its boulder over the boulder's width; 1.1 is the reattachment "
       "length behind a dome, and modelled shadow dunes run 1.3 to 5.6, longer in lighter "
       "winds.",
       "", Above(0.0));
    RadianField(io, "flank_angle_deg", shadows.flankAngle,
                "Slope of a tail's flanks, in degrees, at most the angle of repose (about 32 for "
                "dry sand); sets the tail's height with the boulder's width.",
                Inside(0.0, 90.0));
    io("min_diameter", shadows.minDiameter, "Smallest boulder with a tail, in metres.", "m",
       AtLeast(0.0));
    io("cover_thickness", shadows.coverThickness,
       "Sand depth that fully covers the ground in the material map, in metres.", "m", Above(0.0));
    io("fade", shadows.fade,
       "How tails behind boulders too small for the sample spacing fade out, by samples across "
       "the boulder.");
}
template <typename IO> void Fields(IO& io, SandShadowLayerSpec& spec) {
    Fields(io, spec.shadows);
}

template <> struct SpecName<SandFillLayerSpec> {
    static constexpr std::string_view kName = "sand_fill";
    static constexpr std::string_view kDescription =
        "Sand pooled in the lows of the ground beneath: cracks, troughs and hollows between "
        "rises, laying sand over the material map.";
};
template <typename IO> void Fields(IO& io, SandFillOptions& fill) {
    io("reach", fill.reach,
       "Radius in metres of the surroundings a low is measured against: two rings, at half the "
       "reach and at the reach.",
       "m", Above(0.0));
    io("fill", fill.fill, "Share of a low's depth below its surroundings that sand fills, 0 to 1.",
       "", UnitInterval());
    io("max_thickness", fill.maxThickness,
       "The thickest the sand gets, in metres, approached smoothly.", "m", Above(0.0));
    io("cover_thickness", fill.coverThickness,
       "Sand thickness in metres from which it fully covers the ground in the material map.", "m",
       Above(0.0));
}
template <typename IO> void Fields(IO& io, SandFillLayerSpec& spec) {
    Fields(io, spec.sandFill);
}

template <> struct SpecName<BedformLayerSpec> {
    static constexpr std::string_view kName = "bedforms";
    static constexpr std::string_view kDescription =
        "Fields of wind-blown ripples or dunes added to the terrain.";
};
template <typename IO> void Fields(IO& io, BedformOptions& bedforms) {
    io("wavelength", bedforms.wavelength,
       "Crest-to-crest spacing, in metres; each field's varies from 0.8 to 1.2 times it.", "m",
       Above(0.0));
    io("height", bedforms.height,
       "Trough-to-crest height, in metres, scaled with each field's wavelength.", "m",
       AtLeast(0.0));
    RadianField(io, "wind_azimuth_deg", bedforms.windAzimuth,
                WindAzimuthHelp("wind", "crests lie across it"));
    RadianField(io, "azimuth_spread_deg", bedforms.azimuthSpread,
                "Largest difference of each field's wind from the azimuth, in degrees.",
                AtLeast(0.0));
    io("lee_fraction", bedforms.leeFraction,
       "Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, lower values give "
       "a steeper lee face.",
       "", Inside(0.0, 1.0));
    io("sinuosity", bedforms.sinuosity,
       "How far a crest wanders along its length, in wavelengths; 0 gives straight crests.", "",
       AtLeast(0.0));
    io("cover", bedforms.cover,
       "Share of the ground under bedform fields, from 0 to 0.9; 0 gives no bedforms.", "",
       Between(0.0, 0.9));
    io("field_size", bedforms.fieldSize, "Typical width of a bedform field, in metres.", "m",
       Above(0.0));
    io("seed", bedforms.seed,
       "Seed of the fields and their crests; changing it gives a different layout of the same "
       "statistics.");
    ExposureFields(io, bedforms.exposure, bedforms.exposureSpacing,
                   "Leaves out fields whose centres lie on bedrock, the likelier the barer the "
                   "ground, as sand settles in lows; unset disables it.");
    io("fade", bedforms.fade,
       "How bedforms too fine for the sample spacing fade out, by samples per wavelength.");
}
template <typename IO> void Fields(IO& io, BedformLayerSpec& spec) {
    Fields(io, spec.bedforms);
}

/// The fields a spectral continuation shares between its options and its layer spec.
template <typename IO>
void SpectralSynthesisFields(IO& io, double& minWavelength, double& taperOctaves,
                             std::uint32_t& seed, ResolutionFade& fade) {
    io("min_wavelength", minWavelength,
       "Shortest wavelength of the detail in metres; the detail spans the wavelengths from twice "
       "the base's pixel size down to it.",
       "m", Above(0.0));
    io("taper_octaves", taperOctaves,
       "Octaves above the base's Nyquist frequency over which the detail fades in, so it adds "
       "nothing at wavelengths the base resolves.",
       "", Above(0.0));
    io("seed", seed,
       "Seed of the detail; changing it gives a different pattern of the same "
       "spectrum.");
    io("fade", fade,
       "How wavelengths too fine for the sample spacing fade out, by samples per wavelength.");
}

template <typename IO> void Fields(IO& io, SpectralContinuationOptions& options) {
    io("pixel_size", options.pixelSize,
       "Cell size of the base in metres; the detail starts at twice it, the base's Nyquist "
       "wavelength.",
       "m", Above(0.0));
    io("slope", options.slope,
       "Spectral slope beta of the radially averaged power spectrum, P(f) ~ f^-beta, in [2, 4]: "
       "beta = 2 H + 2 for Hurst exponent H.",
       "", Between(2.0, 4.0));
    io("octave_rms", options.octaveRms,
       "RMS height in metres the power law holds at wavelengths from one to two base pixels.", "m",
       AtLeast(0.0));
    SpectralSynthesisFields(io, options.minWavelength, options.taperOctaves, options.seed,
                            options.fade);
}

template <> struct SpecName<SpectralContinuationLayerSpec> {
    static constexpr std::string_view kName = "spectral_continuation";
    static constexpr std::string_view kDescription =
        "Detail finer than the base continuing its measured power-law spectrum to shorter "
        "wavelengths with random phases, as sparse Gabor noise: an alternative to diamond-square "
        "upsampling without lattice artifacts.";
};
template <typename IO> void Fields(IO& io, SpectralContinuationLayerSpec& spec) {
    io("pixel_size", spec.pixelSize,
       "Cell size of the base in metres; the detail starts at twice it. Unset uses the base's "
       "cell size.",
       "m", Above(0.0));
    io("slope", spec.slope,
       "Spectral slope beta of the radially averaged power spectrum, P(f) ~ f^-beta, in [2, 4]: "
       "beta = 2 H + 2 for Hurst exponent H. Unset fits it to the base.",
       "", Between(2.0, 4.0));
    io("octave_rms", spec.octaveRms,
       "RMS height in metres the power law holds at wavelengths from one to two base pixels. "
       "Unset fits it to the base.",
       "m", AtLeast(0.0));
    io("fit_min_pixels", spec.fit.minPixels,
       "Shortest wavelength, in base pixels, the base's spectrum is fitted over; at least 2.", "",
       AtLeast(2.0));
    io("fit_max_pixels", spec.fit.maxPixels,
       "Longest wavelength, in base pixels, the base's spectrum is fitted over; 0 for a quarter "
       "of the window the spectrum is measured on (at most 512 cells a side).",
       "", AtLeast(0.0));
    SpectralSynthesisFields(io, spec.minWavelength, spec.taperOctaves, spec.seed, spec.fade);
}

template <> struct SpecName<ScaleLayerSpec> {
    static constexpr std::string_view kName = "scale";
    static constexpr std::string_view kDescription =
        "Scales and shifts the heights so far, for vertical exaggeration or a datum shift.";
};
template <typename IO> void Fields(IO& io, ScaleLayerSpec& spec) {
    io("scale", spec.scale,
       "Factor every height so far is multiplied by, as for vertical exaggeration.");
    io("offset", spec.offset, "Metres added to every height after scaling, to shift the datum.",
       "m");
}

template <> struct SpecName<ClampLayerSpec> {
    static constexpr std::string_view kName = "clamp";
    static constexpr std::string_view kDescription =
        "Clamps the heights so far between optional bounds, to flatten a basin floor or cap "
        "peaks.";
};
template <typename IO> void Fields(IO& io, ClampLayerSpec& spec) {
    io("min", spec.min,
       "Lowest height kept, in metres; lower heights are raised to it, flattening basin floors. "
       "Unset leaves heights unbounded below.",
       "m");
    io("max", spec.max,
       "Highest height kept, in metres; higher heights are lowered to it, capping peaks. Unset "
       "leaves heights unbounded above.",
       "m");
}

template <> struct SpecName<RectangleRegionSpec> {
    static constexpr std::string_view kName = "rectangle";
    static constexpr std::string_view kDescription =
        "An axis-aligned rectangle in the surface's coordinates.";
};
template <typename IO> void Fields(IO& io, RectangleRegionSpec& spec) {
    io("min_x", spec.minX,
       "Lower edge along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("min_y", spec.minY,
       "Lower edge along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("max_x", spec.maxX,
       "Upper edge along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("max_y", spec.maxY,
       "Upper edge along y in the surface's coordinates; the latitude in degrees on a sphere.");
}

template <> struct SpecName<CircleRegionSpec> {
    static constexpr std::string_view kName = "circle";
    static constexpr std::string_view kDescription =
        "A circle in the surface's coordinates, its radius in metres.";
};
template <typename IO> void Fields(IO& io, CircleRegionSpec& spec) {
    io("x", spec.x,
       "Centre along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("y", spec.y,
       "Centre along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("radius", spec.radius, "Radius of the circle in metres, also on a sphere.", "m",
       AtLeast(0.0));
}

template <> struct SpecName<RegionLayerSpec> {
    static constexpr std::string_view kName = "region";
    static constexpr std::string_view kDescription =
        "Another layer applied only inside a rectangle or circle, feathered outside it: to "
        "confine craters to an area or flatten a landing pad.";
};
template <typename IO> void Fields(IO& io, RegionLayerSpec& spec) {
    io("shape", spec.shape, "Area the inner layer is applied in: a rectangle or a circle.");
    io("feather", spec.feather,
       "Distance in metres outside the shape over which the inner layer fades back to the "
       "unchanged height; 0 gives a hard edge.",
       "m", AtLeast(0.0));
    io("blend", spec.blend, "Curve of the fade across the feather.");
    io("layer", spec.layer,
       "Layer applied only inside the shape. A crater layer here does not count as the crater "
       "layer for later boulder ejecta or secondaries.");
}

}  // namespace terraingen::config
