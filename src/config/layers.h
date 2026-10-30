// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A configuration's layers: noise, roughness, bedforms, transforms and masked layers, the
/// layer variant, and checking and building every layer in order.
#pragma once

#include "config/boulders.h"
#include "config/craters.h"
#include "config/layer_context.h"

#include "terraingen/analysis/spectrum.h"
#include "terraingen/layers/bedforms_fields.h"
#include "terraingen/layers/dunes_fields.h"
#include "terraingen/layers/masked.h"
#include "terraingen/layers/pavement_fields.h"
#include "terraingen/layers/roughness_estimate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// Layer "noise": a stack of 1 to `kMaxFractals` noise layers added to the terrain
/// (`NoiseLayer`), measured from the source's reference point.
struct NoiseLayerSpec {
    std::vector<FractalSpec> stack{FractalSpec{}};
    /// Whether unset Hurst exponents and amplitudes come from the source (`EstimatedFractals` of
    /// `EstimateSourceRoughness`); needs a source.
    bool estimateFromSource = false;
    /// Source cells across from which the source's measured data holds a noise layer: each layer
    /// whose wavelength, its longest, is at least that many source cells at a point is left out
    /// there (`NoiseLayer`). Ignored on a synthetic source; unset adds every layer everywhere.
    std::optional<double> sourceResolvesPixels{};

    /// The stack as fractals, with each layer's Hurst exponent applied.
    std::vector<Fractal> Fractals() const;

    /// `Fractals` with each layer's unset Hurst exponent `source.hurst` and unset amplitude the one
    /// that continues `source` at the layer's Hurst exponent to a lag of its wavelength
    /// (`MatchedNoiseAmplitude`).
    /// @throws std::invalid_argument as `MatchedNoiseAmplitude`.
    std::vector<Fractal> EstimatedFractals(const BaseRoughness& source) const;
};

/// Layer "roughness": fine roughness of a given RMS slope (`RoughnessLayer`), or of the RMS slope
/// and Hurst exponent the source's variogram continues to (`EstimateSourceRoughness`).
struct RoughnessLayerSpec {
    /// The roughness; its `rmsSlope` and `hurst` stand in for those below when they are unset and
    /// nothing is estimated.
    RoughnessOptions roughness{};
    std::optional<double> rmsSlope{};  ///< Rise over run; unset for the default or the estimate.
    std::optional<double> hurst{};     ///< Unset for the default or the estimate.
    /// Whether unset `rmsSlope` and `hurst` come from the source (`EstimatedOptions` of
    /// `EstimateSourceRoughness`); needs a source.
    bool estimateFromSource = false;

    /// `roughness` with `rmsSlope` and `hurst` in place where set.
    RoughnessOptions Options() const;

    /// `Options` with an unset `hurst` `source.hurst` and an unset `rmsSlope` the RMS slope over
    /// the baseline that continues `source` at the Hurst exponent (`BaseRoughness::RmsSlope`).
    RoughnessOptions EstimatedOptions(BaseRoughness source) const;
};

/// Layer "spectral_continuation": detail below the source's resolution continuing its power-law
/// spectrum (`SpectralContinuationLayer`), fitted to the source (`EstimatePowerLawSpectrum`) on its
/// own grid wherever the slope or amplitude is not given.
struct SpectralContinuationLayerSpec {
    std::optional<double> pixelSize{};  ///< Meters; unset for the source's cell size.
    std::optional<double> slope{};      ///< Spectral slope beta; unset to fit from the source.
    std::optional<double> octaveRms{};  ///< Meters; unset to fit from the source.
    SpectrumFitBand fit{};              ///< Wavelengths the source's spectrum is fitted over.
    double minWavelength = 0.05;        ///< Meters.
    double taperOctaves = 1.0;
    std::uint32_t seed = 1;
    ResolutionFade fade{};

    /// The layer's options, the defaults in place of an unset pixel size, slope and amplitude.
    SpectralContinuationOptions Options() const;
};

/// Layer "scale": height * `scale` + `offset` (`ScaleLayer`).
struct ScaleLayerSpec {
    double scale = 1.0;
    double offset = 0.0;  ///< Meters.
};

/// Layer "clamp": heights clamped to [`min`, `max`], either optional (`ClampLayer`).
struct ClampLayerSpec {
    std::optional<double> min;  ///< Meters; unset for no lower bound.
    std::optional<double> max;  ///< Meters; unset for no upper bound.
};

/// Region shape "rectangle": [`minX`, `maxX`] x [`minY`, `maxY`] in surface coordinates.
struct RectangleRegionSpec {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
};
/// Region shape "circle": centered on (`x`, `y`) in surface coordinates.
struct CircleRegionSpec {
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;  ///< Meters.
};
/// Region shape "polygon": outlined by `points`, less `holes`, in surface coordinates.
struct PolygonRegionSpec {
    std::vector<std::array<double, 2>> points{};              ///< [x, y] of each vertex.
    std::vector<std::vector<std::array<double, 2>>> holes{};  ///< Each hole's vertices.
};
/// A region shape, named in JSON by its "type".
using RegionShapeSpec = std::variant<RectangleRegionSpec, CircleRegionSpec, PolygonRegionSpec>;

struct MaskedLayerSpec;

/// A layer, named in JSON by its "type". Layers that take only their library options hold them
/// directly: "dunes", "bedforms", "pavement", "strata", "sand_shadow" and "sand_fill".
using LayerSpec =
    std::variant<NoiseLayerSpec, CraterLayerSpec, BoulderLayerSpec, RoughnessLayerSpec,
                 BedformOptions, ScaleLayerSpec, ClampLayerSpec, MaskedLayerSpec,
                 SecondaryLayerSpec, DuneOptions, SandFillOptions, PavementOptions, StrataOptions,
                 SandShadowOptions, SpectralContinuationLayerSpec>;

/// Layer "masked": `layer` applied only inside `shape` (`MaskedLayer`). Boulder ejecta do not see
/// crater layers inside masked layers.
struct MaskedLayerSpec {
    RegionShapeSpec shape{};
    double feather = 0.0;  ///< Meters outside the shape over which the layer fades out.
    BlendShape blend = BlendShape::Smoothstep;
    Boxed<LayerSpec> layer{};
};

template <> struct SpecName<NoiseLayerSpec> {
    static constexpr std::string_view kName = "noise";
    static constexpr std::string_view kDescription =
        "A stack of fractal noise layers added to the terrain, for hills, ridges and rolling "
        "relief.";
};
template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer) {
    io("stack", layer.stack, std::string(kNoiseStackHelp), "", NoiseStackLength(1));
    io("estimate_from_source", layer.estimateFromSource,
       "Continues the source's roughness into the noise: each layer's unset hurst becomes the "
       "Hurst exponent of the source's variogram at its native cell size, and each unset "
       "amplitude gives the layer the RMS height difference that variogram extrapolates to at "
       "a lag of the layer's wavelength. Meant for wavelengths at or below the source's cell "
       "size; needs a source with posts.");
    SourceResolvesField(io, layer.sourceResolvesPixels, "noise layers of the stack",
                        "wavelength, the longest of each layer's octaves");
}

template <> struct SpecName<RoughnessLayerSpec> {
    static constexpr std::string_view kName = "roughness";
    static constexpr std::string_view kDescription =
        "Fine roughness from octaves of noise, scaled to a given RMS slope over a baseline.";
};
template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec) {
    io("rms_slope", spec.rmsSlope,
       std::string(kRmsSlopeHelp) +
           " Unset is 0.15, or with estimate_from_source the source's RMS slope extrapolated to "
           "the baseline.",
       "", Above(0.0));
    io("baseline_m", spec.roughness.baseline, "Baseline the RMS slope is given over, in meters.",
       "m", Above(0.0));
    io("hurst", spec.hurst,
       std::string(kRoughnessHurstHelp) +
           " Unset is 0.8, or with estimate_from_source the source's.",
       "", AboveUpTo(0.0, 1.0));
    io("estimate_from_source", spec.estimateFromSource,
       "Continues the source's roughness below its resolution: fits the Hurst exponent and RMS "
       "height difference to the source's variogram at lags of 1 to 8 native cells, over up to "
       "512 cells about its center, and extrapolates the RMS slope to the baseline. Set "
       "rms_slope or hurst to override an estimate. Needs a source with posts and relief; keep "
       "max_wavelength_m near the source's cell size.");
    RoughnessOctaveFields(io, spec.roughness);
}

template <> struct SpecName<DuneOptions> {
    static constexpr std::string_view kName = "dunes";
    static constexpr std::string_view kDescription =
        "Wind-blown dunes added to the terrain: isolated barchans or domes, or fields of "
        "transverse, barchanoid or linear crests, with slip faces and optional ripples on them.";
};

template <> struct SpecName<PavementOptions> {
    static constexpr std::string_view kName = "pavement";
    static constexpr std::string_view kDescription =
        "Fractured bedrock: polygonal plates, each tilted and stepped, parted by cracks or raised "
        "ridges, split again by finer octaves of cracks; lays bedrock over the material map.";
};

template <> struct SpecName<StrataOptions> {
    static constexpr std::string_view kName = "strata";
    static constexpr std::string_view kDescription =
        "Layered rock: the ground beneath terraced into dipping beds, each a flat bench below a "
        "cliff, so hills become stepped buttes and mesas; lays bedrock over the cliffs.";
};

template <> struct SpecName<SandShadowOptions> {
    static constexpr std::string_view kName = "sand_shadow";
    static constexpr std::string_view kDescription =
        "Sand shadows: a tapering sand tail in the lee of each boulder of the nearest boulder "
        "layer before this one; lays sand over the material map.";
};

template <> struct SpecName<SandFillOptions> {
    static constexpr std::string_view kName = "sand_fill";
    static constexpr std::string_view kDescription =
        "Sand pooled in the lows of the ground beneath: cracks, troughs and hollows between "
        "rises, laying sand over the material map.";
};

template <> struct SpecName<BedformOptions> {
    static constexpr std::string_view kName = "bedforms";
    static constexpr std::string_view kDescription =
        "Fields of wind-blown ripples or dunes added to the terrain.";
};

template <> struct SpecName<SpectralContinuationLayerSpec> {
    static constexpr std::string_view kName = "spectral_continuation";
    static constexpr std::string_view kDescription =
        "Detail finer than the source continuing its measured power-law spectrum to shorter "
        "wavelengths with random phases, as sparse Gabor noise: an alternative to diamond-square "
        "upsampling without lattice artifacts.";
};
template <typename IO> void Fields(IO& io, SpectralContinuationLayerSpec& spec) {
    io("pixel_size_m", spec.pixelSize,
       "Cell size of the source in meters; the detail starts at twice it. Unset uses the "
       "source's cell size.",
       "m", Above(0.0));
    io("slope", spec.slope,
       "Spectral slope beta of the radially averaged power spectrum, P(f) ~ f^-beta: beta = 2 H "
       "+ 2 for Hurst exponent H. Unset fits it to the source.",
       "", Between(2.0, 4.0));
    io("octave_rms_m", spec.octaveRms,
       "RMS height in meters the power law holds at wavelengths from one to two source pixels. "
       "Unset fits it to the source.",
       "m", AtLeast(0.0));
    io("fit_min_pixels", spec.fit.minPixels,
       "Shortest wavelength, in source pixels, the source's spectrum is fitted over.", "",
       AtLeast(2.0));
    io("fit_max_pixels", spec.fit.maxPixels,
       "Longest wavelength, in source pixels, the source's spectrum is fitted over; 0 for a "
       "quarter of the window the spectrum is measured on (at most 512 cells a side).",
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
    io("offset_m", spec.offset, "Meters added to every height after scaling, to shift the datum.",
       "m");
}

template <> struct SpecName<ClampLayerSpec> {
    static constexpr std::string_view kName = "clamp";
    static constexpr std::string_view kDescription =
        "Clamps the heights so far between optional bounds, to flatten a basin floor or cap "
        "peaks.";
};
template <typename IO> void Fields(IO& io, ClampLayerSpec& spec) {
    io("min_m", spec.min,
       "Lowest height kept, in meters; lower heights are raised to it, flattening basin floors. "
       "Unset leaves heights unbounded below.",
       "m");
    io("max_m", spec.max,
       "Highest height kept, in meters; higher heights are lowered to it, capping peaks. Unset "
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
        "A circle in the surface's coordinates, its radius in meters.";
};
template <typename IO> void Fields(IO& io, CircleRegionSpec& spec) {
    io("x", spec.x,
       "Center along x in the surface's coordinates; the longitude in degrees on a sphere.");
    io("y", spec.y,
       "Center along y in the surface's coordinates; the latitude in degrees on a sphere.");
    io("radius_m", spec.radius, "Radius of the circle in meters, also on a sphere.", "m",
       AtLeast(0.0));
}

template <> struct SpecName<PolygonRegionSpec> {
    static constexpr std::string_view kName = "polygon";
    static constexpr std::string_view kDescription =
        "A simple polygon in the surface's coordinates, concave or with holes: to follow a "
        "mapped unit or a traverse corridor.";
};
template <typename IO> void Fields(IO& io, PolygonRegionSpec& spec) {
    io("points", spec.points,
       "Vertices of the outline as [x, y] pairs, x east and y north in the surface's coordinates "
       "(longitude and latitude in degrees on a sphere), closed from the last back to the first, "
       "so the first is not repeated. Points inside an odd number of outlines and holes are "
       "inside.",
       "", MinLength(3));
    io("holes", spec.holes,
       "Outlines cut out of the polygon, each a list of at least 3 [x, y] vertices as `points`; "
       "the masked layer fades out from their edges as from the outline's.");
}

template <> struct SpecName<MaskedLayerSpec> {
    static constexpr std::string_view kName = "masked";
    static constexpr std::string_view kDescription =
        "Another layer applied only inside a rectangle, circle or polygon, feathered outside it: "
        "to confine craters to an area or flatten a landing pad.";
};
template <typename IO> void Fields(IO& io, MaskedLayerSpec& spec) {
    io("shape", spec.shape,
       "Area the inner layer is applied in: a rectangle, a circle or a polygon.");
    io("feather_m", spec.feather,
       "Distance in meters outside the shape over which the inner layer fades back to the "
       "unchanged height; 0 gives a hard edge.",
       "m", AtLeast(0.0));
    io("blend", spec.blend, "Curve of the fade across the feather.");
    io("layer", spec.layer,
       "Layer applied only inside the shape. A crater layer here does not count as the crater "
       "layer for later boulder ejecta or secondaries.");
}

/// A layer's JSON "type", with the inner layer's type for a masked one: "masked (boulders)".
std::string LayerType(const LayerSpec& layer);

namespace config {

/// The number of `layers` of the kinds `Kinds`; layers inside masked layers do not count.
template <typename... Kinds> std::size_t CountLayers(const std::vector<LayerSpec>& layers) {
    return static_cast<std::size_t>(
        std::count_if(layers.begin(), layers.end(),
                      [](const LayerSpec& layer) { return HoldsAny<Kinds...>(layer); }));
}

/// The indices of the `layers` of the kinds `Kinds`, in order; layers inside masked layers do not
/// count.
template <typename... Kinds>
std::vector<std::size_t> LayerIndices(const std::vector<LayerSpec>& layers) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (HoldsAny<Kinds...>(layers[i])) {
            indices.push_back(i);
        }
    }
    return indices;
}

/// The region shape a spec describes, unchecked.
RegionShape BuildRegionShape(const RegionShapeSpec& spec);

/// The sources a terrain's layers are built on.
struct LayerSources {
    /// The terrain's source: the one the configuration or the caller gives, or the default
    /// synthetic noise.
    std::shared_ptr<const Source> terrain;
    /// The source the configuration or the caller gives; null for the default synthetic noise.
    std::shared_ptr<const Source> given;
    /// The source's measured data, which layers leave out what it resolves of; null for none.
    std::shared_ptr<const Source> measured;
    bool posts = false;  ///< Whether the source has posts that layers can estimate settings from.
};

/// A configuration's layers, built in order.
struct BuiltLayers {
    std::vector<LayerPtr> layers;
    /// The most craters the top-level crater layers are expected to generate over one extent;
    /// none when none generate.
    std::optional<double> expectedCraters;
};

/// Checks every layer spec, then builds each layer in order on `surface` and checks it against
/// what it built, over the outputs' `extents`.
/// @throws ConfigError under "layers" for an invalid layer.
BuiltLayers BuildLayers(const std::vector<LayerSpec>& layers,
                        const std::vector<OutputExtent>& extents, const LayerSources& sources,
                        const std::filesystem::path& directory, const Surface& surface);

}  // namespace config

}  // namespace terraingen
