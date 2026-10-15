/// @file
/// @brief Internal: the JSON fields of the noise, roughness, bedform, transform and region layers.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/detail/fields_common.h"

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
