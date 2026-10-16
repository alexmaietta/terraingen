/// @file
/// @brief The JSON fields of the noise, roughness, bedform, transform and region layers.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

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
       "m");
    io("amplitude", fractal.amplitude, "Largest height of the layer above or below 0, in metres.",
       "m");
    io("octaves", fractal.octaves,
       "Number of octaves, at least 1; each one adds detail at a shorter wavelength.");
    io("lacunarity", fractal.lacunarity,
       "Frequency ratio between successive octaves; greater than 1.");
    io("gain", fractal.gain,
       "Weight ratio between successive octaves, greater than 0; higher values give rougher "
       "terrain. Unused when the Hurst exponent is set.");
    io("hurst", config.hurst,
       "Hurst exponent that sets the octave gain to lacunarity^-hurst: near 1 is smooth, lower is "
       "rougher at short baselines. Unset uses the gain.");
    io("offset", fractal.offset,
       "Ridge offset of the ridged and hybrid multifractal types; greater than 0.");
    io("warp_amplitude", layer.warpAmplitude,
       "Domain warp displacement in metres, which bends the noise into swirled shapes; 0 "
       "disables warping.",
       "m");
    io("warp_wavelength", layer.warpWavelength, "Wavelength of the warp field, in metres.", "m");
}

template <> struct SpecName<NoiseLayerSpec> {
    static constexpr std::string_view kName = "noise";
    static constexpr std::string_view kDescription =
        "A stack of fractal noise layers added to the terrain, for hills, ridges and rolling "
        "relief.";
};
template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer) {
    io("stack", layer.stack,
       "Noise layers, 1 to 8, combined in order: the first enabled layer sets the height and each "
       "later one is combined with it by its blend mode.");
}

template <> struct SpecName<RoughnessLayerSpec> {
    static constexpr std::string_view kName = "roughness";
    static constexpr std::string_view kDescription =
        "Fine roughness from octaves of noise, scaled to a given RMS slope over a baseline.";
};
template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec) {
    RoughnessOptions& roughness = spec.roughness;
    io("rms_slope", roughness.rmsSlope,
       "RMS slope, as rise over run, over the baseline when every octave is resolved; higher "
       "values give rougher ground.");
    io("baseline", roughness.baseline, "Baseline the RMS slope is given over, in metres.", "m");
    io("hurst", roughness.hurst,
       "Hurst exponent, in (0, 1]: octave amplitudes grow as wavelength^hurst, so lower values "
       "put relatively more roughness at short wavelengths.");
    io("max_wavelength", roughness.maxWavelength, "Wavelength of the longest octave, in metres.",
       "m");
    io("min_wavelength", roughness.minWavelength,
       "Wavelength of the shortest octave, in metres; octaves halve from the longest down to it, "
       "spanning at most 40 octaves.",
       "m");
    io("seed", roughness.seed,
       "Seed of the noise; changing it gives a different pattern of the same roughness.");
    io("fade", roughness.fade,
       "How octaves too fine to resolve at the sample spacing fade out, by samples per "
       "wavelength.");
}

template <typename IO> void Fields(IO& io, DuneRipples& ripples) {
    io("wavelength", ripples.wavelength,
       "Crest-to-crest spacing of the ripples on flat sand, in metres; 0 gives no ripples.", "m");
    io("height", ripples.height, "Trough-to-crest height of the ripples on flat sand, in metres.",
       "m");
    io("lee_fraction", ripples.leeFraction,
       "Share of a ripple wavelength the lee face takes, in (0, 1): 0.5 is symmetric, lower "
       "values give a steeper lee face.");
    io("sinuosity", ripples.sinuosity,
       "How far a ripple crest wanders along its length, in wavelengths.");
    io("slope_coupling", ripples.slopeCoupling,
       "How strongly the dune's slope turns and tightens the ripples: they shorten on steep lee "
       "faces and turn on the flanks; 0 keeps them straight across the wind.");
    io("interdune_cover", ripples.interduneCover,
       "Share of the ripple height kept on the ground between dunes, from 0 to 1.");
    io("forks", ripples.forks,
       "Chance, from 0 to 1, that each patch of 8 x 8 ripple wavelengths holds a pair of forked "
       "crests (Y-junctions).");
    io("fade", ripples.fade,
       "How ripples too fine for the sample spacing fade out, by samples per wavelength.");
}

template <> struct SpecName<DuneLayerSpec> {
    static constexpr std::string_view kName = "dunes";
    static constexpr std::string_view kDescription =
        "Wind-blown dunes added to the terrain: isolated barchans or domes, or fields of "
        "transverse, barchanoid or linear crests, with slip faces and optional ripples on them.";
};
template <typename IO> void Fields(IO& io, DuneLayerSpec& spec) {
    DuneOptions& dunes = spec.dunes;
    io("shape", dunes.shape, "The kind of dune.");
    io("wind_azimuth", dunes.windAzimuth,
       "Direction the wind blows toward, in radians counter-clockwise from +x (local east of the "
       "anchor on a body); slip faces face it.",
       "rad");
    io("azimuth_spread", dunes.azimuthSpread,
       "Largest difference of each barchan or dome's wind from the azimuth, in radians.", "rad");
    io("slip_face_angle", dunes.slipFaceAngle,
       "Slope of the lee slip faces, in radians: the sand's angle of repose.", "rad");
    io("stoss_slope", dunes.stossSlope,
       "Mean slope of the windward faces, in radians; below the slip face angle.", "rad");
    io("stoss_exponent", dunes.stossExponent,
       "Exponent alpha of the cos^alpha windward profile of dunes; higher is blunter.");
    io("dome_exponent", dunes.domeExponent, "Exponent alpha of the cos^alpha profile of domes.");
    io("spacing", dunes.spacing,
       "Barchans and domes: side of the lattice cells, each holding at most one dune, in metres.",
       "m");
    io("density", dunes.density, "Barchans and domes: chance a cell holds a dune, from 0 to 1.");
    io("width", dunes.width, "Barchans and domes: mean width, horn to horn, in metres.", "m");
    io("width_spread", dunes.widthSpread,
       "Barchans and domes: each width varies from (1 - spread) to (1 + spread) times the mean, "
       "spread in [0, 1).");
    io("min_width", dunes.minWidth,
       "Barchans: width in metres below which a dune is a dome without a slip face, blending "
       "into barchans up to 1.5 times it.",
       "m");
    io("height_ratio", dunes.heightRatio, "Barchans and domes: height over width.");
    io("horn_length", dunes.hornLength,
       "Barchans: how far the horns reach downwind of the crest, in dune heights.");
    io("dome_length", dunes.domeLength, "Domes: length along the wind over width.");
    io("wavelength", dunes.wavelength,
       "Transverse, barchanoid and linear dunes: crest-to-crest spacing, in metres.", "m");
    io("height", dunes.height, "Transverse, barchanoid and linear dunes: crest height, in metres.",
       "m");
    io("sinuosity", dunes.sinuosity,
       "Transverse, barchanoid and linear dunes: how far a crest wanders along its length, in "
       "wavelengths.");
    io("defects", dunes.defects,
       "Transverse, barchanoid and linear dunes: share of the height that varies along crests, "
       "lowering and ending some, from 0 to 1.");
    io("flank_slope", dunes.flankSlope, "Linear dunes: mean slope of both flanks, in radians.",
       "rad");
    io("seed", dunes.seed,
       "Seed of the dunes and ripples; changing it gives a different layout of the same "
       "statistics.");
    io("anchor_x", dunes.anchor.x,
       "On a body, the longitude in degrees of the point whose tangent frame the dunes are laid "
       "out in; they are true to scale near it and absent on the far hemisphere.");
    io("anchor_y", dunes.anchor.y, "On a body, the latitude in degrees of the anchor.");
    io("fade", dunes.fade,
       "How dunes too fine for the sample spacing fade out, by samples across their width or "
       "wavelength.");
    io("ripples", dunes.ripples, "Ripples superposed on the dunes.");
}

template <> struct SpecName<BedformLayerSpec> {
    static constexpr std::string_view kName = "bedforms";
    static constexpr std::string_view kDescription =
        "Fields of wind-blown ripples or dunes added to the terrain.";
};
template <typename IO> void Fields(IO& io, BedformLayerSpec& spec) {
    BedformOptions& bedforms = spec.bedforms;
    io("wavelength", bedforms.wavelength,
       "Crest-to-crest spacing, in metres; each field's varies from 0.8 to 1.2 times it.", "m");
    io("height", bedforms.height,
       "Trough-to-crest height, in metres, scaled with each field's wavelength.", "m");
    io("wind_azimuth", bedforms.windAzimuth,
       "Direction the wind blows toward, in radians counter-clockwise from +x (local east on a "
       "sphere); crests lie across it.",
       "rad");
    io("azimuth_spread", bedforms.azimuthSpread,
       "Largest difference of each field's wind from the azimuth, in radians.", "rad");
    io("lee_fraction", bedforms.leeFraction,
       "Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, lower values give "
       "a steeper lee face.");
    io("sinuosity", bedforms.sinuosity,
       "How far a crest wanders along its length, in wavelengths; 0 gives straight crests.");
    io("cover", bedforms.cover,
       "Share of the ground under bedform fields, from 0 to 0.9; 0 gives no bedforms.");
    io("field_size", bedforms.fieldSize, "Typical width of a bedform field, in metres.", "m");
    io("seed", bedforms.seed,
       "Seed of the fields and their crests; changing it gives a different layout of the same "
       "statistics.");
    io("exposure", bedforms.exposure,
       "Leaves out fields whose centres lie on bedrock, the likelier the barer the ground, as "
       "sand settles in lows; unset disables it.");
    io("exposure_spacing", bedforms.exposureSpacing,
       "Spacing in metres the ground is read at for the exposure field.", "m");
    io("fade", bedforms.fade,
       "How bedforms too fine for the sample spacing fade out, by samples per wavelength.");
}

template <> struct SpecName<DemDetailLayerSpec> {
    static constexpr std::string_view kName = "dem_detail";
    static constexpr std::string_view kDescription =
        "Adds the detail a GeoTIFF DEM has above its own low-pass, to restore measured relief over "
        "a "
        "raster source read with the same low_pass.";
};
template <typename IO> void Fields(IO& io, DemDetailLayerSpec& spec) {
    io("path", spec.path, InputPathHelp("GeoTIFF DEM to take the detail from"));
    io("band", spec.read.band, "Band of the file to read, counting from 0.");
    io("resampling", spec.read.resampling, "Interpolation used between the DEM's cells.");
    io("scale", spec.read.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset", spec.read.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
    io("low_pass", spec.read.lowPass,
       "Standard deviation in metres of the Gaussian low-pass the detail is measured above; "
       "positive, and the same as the raster source's low_pass to restore the DEM whole.",
       "m");
    io("weight", spec.weight, "Share of the detail added; 1 restores it whole, 0.5 halves it.");
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
    io("radius", spec.radius, "Radius of the circle in metres, also on a sphere.", "m");
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
       "m");
    io("blend", spec.blend, "Curve of the fade across the feather.");
    io("layer", spec.layer,
       "Layer applied only inside the shape. A crater layer here does not count as the crater "
       "layer for later boulder ejecta or secondaries.");
}

}  // namespace terraingen::config
