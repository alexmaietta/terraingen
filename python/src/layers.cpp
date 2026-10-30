// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/masked.h"
#include "terraingen/layers/material.h"
#include "terraingen/layers/noise_layer.h"
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/roughness_estimate.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/layers/strata.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/noise/structure_function.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief A layer written in Python: `apply_at(height, x, y, context)` changes one height,
/// `apply(heights, context)` a whole raster at once (faster with NumPy); either is enough.
class PyLayer : public Layer {
  public:
    NB_TRAMPOLINE(Layer);

    LayerPtr Reseeded(std::uint64_t seed) const override {
        return CallOverride<std::shared_ptr<Layer>>(nb_trampoline, "reseeded", seed)
            .value_or(nullptr);
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override {
        Raster<float>* grid = samples.Grid();
        if (grid != nullptr &&
            CallOverrideIgnoringResult(nb_trampoline, "apply", ByReference<Raster<float>>{*grid},
                                       context)) {
            return;
        }
        const bool pointwise = Overrides(nb_trampoline, "apply_at");
        if (!pointwise && !Overrides(nb_trampoline, "apply")) {
            throw nb::type_error("A Python Layer must define apply_at or apply");
        }
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (samples.IsNoData(i)) {
                continue;
            }
            const Vec2d p = samples.Position(i);
            float& height = samples.Height(i);
            height = pointwise ? *CallOverride<float>(nb_trampoline, "apply_at", height, p.x, p.y,
                                                      context)
                               : ApplyToOneCell(height, p, context);
        }
    }

  private:
    /// `apply` on a raster of one cell of side `context.spacing` centered at `p`.
    float ApplyToOneCell(float height, const Vec2d& p, const LayerContext& context) const {
        Raster<float> cell(1, 1, height);
        cell.SetTransform(GeoTransform::NorthUp(p.x - 0.5 * context.spacing,
                                                p.y + 0.5 * context.spacing, context.spacing));
        CallOverrideIgnoringResult(nb_trampoline, "apply", ByReference<Raster<float>>{cell},
                                   context);
        return cell(0, 0);
    }
};

/// @brief A ground written in Python: `fill_samples(samples, spacing)` sets every height of a
/// SampleBlock.
class PyGround : public Ground {
  public:
    NB_TRAMPOLINE(Ground);

  protected:
    void FillSamples(SampleBlock& samples, double spacing) const override {
        CallRequired(
            nb_trampoline, "fill_samples", [](nb::handle /*result*/) {},
            ByReference<SampleBlock>{samples}, spacing);
    }
};

void BindGround(nb::module_& m) {
    nb::class_<Ground, PyGround>(
        m, "Ground",
        "The terrain beneath a layer, readable anywhere: a Terrain, or a prefix of one "
        "(`Terrain.ground`). Subclass it and implement `fill_samples(samples, spacing)`, which "
        "sets every height of a SampleBlock (NaN where there is no data), for a ground of your "
        "own.")
        .def(nb::init<>())
        .def("height_at", &Ground::HeightAt, "x"_a, "y"_a, "spacing"_a,
             nb::call_guard<nb::gil_scoped_release>(),
             "The height at (x, y) sampled every `spacing` meters, NaN where there is no data; "
             "equal to the cell centered there of a grid.")
        .def(
            "heights_at",
            [](const Ground& ground, const CpuMatrix<double>& points, double spacing) {
                std::vector<Vec2d> positions;
                for (const auto& row : Rows<2>(points)) {
                    positions.push_back({row[0], row[1]});
                }
                std::vector<float> heights(positions.size());
                {
                    const nb::gil_scoped_release release;
                    ground.HeightsAt(Span<const Vec2d>(positions), Span<float>(heights), spacing);
                }
                return Column(std::move(heights));
            },
            "points"_a, "spacing"_a,
            "`height_at` each row of `points`, an array of shape (n, 2), as a float32 array.");
}

void BindLayerContext(nb::module_& m) {
    nb::class_<LayerContext>(
        m, "LayerContext",
        "How a layer is evaluated: the sample spacing (m), threads, the `ground` beneath it, the "
        "`surface` the terrain lies on (None for a plane) and its `source` (None for none). The "
        "context keeps what it is given alive.")
        .def(
            "__init__",
            [](LayerContext* self, double spacing, unsigned threads, const Ground* ground,
               const Surface* surface, const Source* source) {
                new (self) LayerContext{};
                self->spacing = spacing;
                self->threads = threads;
                self->ground = ground;
                self->surface = surface;
                self->source = source;
            },
            "spacing"_a = 0.0, "threads"_a = 1, "ground"_a = nb::none(), "surface"_a = nb::none(),
            "source"_a = nb::none(), nb::keep_alive<1, 4>(), nb::keep_alive<1, 5>(),
            nb::keep_alive<1, 6>())
        .def_ro("spacing", &LayerContext::spacing)
        .def_ro("threads", &LayerContext::threads)
        .def_prop_ro(
            "ground", [](const LayerContext& c) { return c.ground; }, nb::rv_policy::reference,
            "The ground beneath the layer; None where none is needed.")
        .def_prop_ro(
            "surface", [](const LayerContext& c) { return c.surface; }, nb::rv_policy::reference,
            "What the terrain lies on; None for a plane.")
        .def_prop_ro(
            "source", [](const LayerContext& c) { return c.source; }, nb::rv_policy::reference,
            "The terrain's starting heights; None for none.")
        .def("surface_or_plane", &LayerContext::SurfaceOrPlane,
             "The surface the terrain lies on, or a plane without one.")
        .def("reference_point", &LayerContext::ReferencePoint,
             "Where noise on a plane is measured from: the source's reference point, or the "
             "origin without a source.");
}

/// @brief `replaced`, pairs of a layer and its replacement, as `Layer::Rebound` takes them.
LayerReplacements Replacements(const std::vector<std::pair<LayerPtr, LayerPtr>>& replaced) {
    LayerReplacements replacements;
    for (const auto& [layer, replacement] : replaced) {
        replacements.emplace_back(layer.get(), replacement);
    }
    return replacements;
}

void BindLayerBase(nb::module_& m) {
    BindGround(m);
    BindLayerContext(m);
    nb::class_<Layer, PyLayer>(
        m, "Layer",
        "Relief added to a terrain. Subclass it and implement `apply_at(height, x, y, context)` "
        "or `apply(heights, context)` (a Raster changed in place), and optionally "
        "`reseeded(seed)`, for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Layer& layer, Raster<float>& heights, const LayerContext& context) {
                layer.Apply(heights, context);
            },
            "heights"_a, "context"_a, nb::call_guard<nb::gil_scoped_release>())
        .def("apply_at", &Layer::ApplyAt, "height"_a, "x"_a, "y"_a, "context"_a)
        .def("layer_surface", &Layer::LayerSurface,
             "The surface a layer that derives state from it when built (craters, boulders) was "
             "built for, which a terrain's must be; None for a layer that reads it from its "
             "context.")
        .def("reseeded", &Layer::Reseeded, "seed"_a,
             "A copy of the layer whose seeds draw from `seed`; None for a layer without seeds.")
        .def(
            "rebound",
            [](const Layer& layer, const std::vector<std::pair<LayerPtr, LayerPtr>>& replaced) {
                return layer.Rebound(Replacements(replaced));
            },
            "replaced"_a,
            "A copy of the layer reading the replacement in each (layer, replacement) pair of "
            "`replaced` in place of that layer; None when it reads none of them.");
}

void BindFades(nb::module_& m) {
    BindEnum<BlendShape>(m, "BlendShape", "How a weight falls from 1 to 0.");
    BindOptions<ResolutionFade>(
        m, "ResolutionFade",
        "How features too small to resolve fade out, by how many samples span them: absent up "
        "to `start_samples`, whole from `end_samples`.",
        {&ValidateResolutionFade, "validate_resolution_fade", "fade"});
    m.def("falloff", &Falloff, "x"_a, "start"_a, "end"_a, "shape"_a,
          "The weight at `x` of a falloff from 1 at `start` to 0 at `end` along `shape`.");
    m.def("smoothstep", &Smoothstep, "t"_a, "The cubic smoothstep of `t` clamped to [0, 1].");
    m.def("resolution_weight", &ResolutionWeight, "diameter"_a, "spacing"_a, "fade"_a,
          "The weight of a feature `diameter` across sampled every `spacing`.");
}

void BindTransforms(nb::module_& m) {
    nb::class_<NoiseLayerOptions> options(
        m, "NoiseLayerOptions",
        "The noise a NoiseLayer adds: `fractals`, 1 to 8 fractals, and with a source "
        "`source_resolves_px`, the source cells across from which a fractal is left out.");
    options.def_rw("fractals", &NoiseLayerOptions::fractals)
        .def_rw("source_resolves_px", &NoiseLayerOptions::sourceResolvesPixels,
                nb::arg("value").none());
    BindKwargsInit<NoiseLayerOptions>(options);
    const Fractal defaults{};
    nb::class_<NoiseLayer, Layer>(m, "NoiseLayer",
                                  "A stack of fractals added to the terrain, measured on a "
                                  "plane from the source's reference point.")
        .def(nb::init<NoiseLayerOptions>(), "options"_a,
             "On a sphere the noise is 3D at each point's position, seamless everywhere. With a "
             "source and `source_resolves_px`, each fractal whose wavelength is at least that "
             "many of the source's cells at a point is left out there.")
        .def(nb::init<std::vector<Fractal>>(), "fractals"_a, "Noise of the stack `fractals`.")
        .def(nb::init<Fractal>(), "fractal"_a, "Noise of the one fractal `fractal`.")
        .def(
            "__init__",
            [](NoiseLayer* self, std::uint32_t seed, float wavelength, float amplitude, int octaves,
               float lacunarity, float gain, NoiseBasis basis, FractalType type, float offset,
               FractalBlend blend, float warpAmplitude, float warpWavelength, bool enabled) {
                const Fractal fractal{enabled,    seed,          wavelength,    amplitude, octaves,
                                      lacunarity, gain,          basis,         type,      offset,
                                      blend,      warpAmplitude, warpWavelength};
                new (self) NoiseLayer(fractal);
            },
            nb::kw_only(), "seed"_a = defaults.seed, "wavelength"_a = defaults.wavelength,
            "amplitude"_a = defaults.amplitude, "octaves"_a = defaults.octaves,
            "lacunarity"_a = defaults.lacunarity, "gain"_a = defaults.gain,
            "basis"_a = defaults.basis, "type"_a = defaults.type, "offset"_a = defaults.offset,
            "blend"_a = defaults.blend, "warp_amplitude"_a = defaults.warpAmplitude,
            "warp_wavelength"_a = defaults.warpWavelength, "enabled"_a = defaults.enabled,
            "Noise of one fractal made from the Fractal keywords.")
        .def_prop_ro("options", &NoiseLayer::Options,
                     "The fractals, with any Hurst exponents and amplitudes applied, and the "
                     "source limit.");
    nb::class_<ScaleLayer, Layer>(m, "ScaleLayer", "height * scale + offset.")
        .def(nb::init<double, double>(), "scale"_a, "offset"_a = 0.0);
    nb::class_<ClampLayer, Layer>(m, "ClampLayer", "Heights held between `min` and `max`.")
        .def(nb::init<std::optional<double>, std::optional<double>>(), "min"_a = nb::none(),
             "max"_a = nb::none());
    nb::class_<FunctionLayer, Layer>(
        m, "FunctionLayer",
        "A layer from a function `fn(x, y, spacing, height)` returning the new height; simpler "
        "than subclassing Layer for point-wise changes. The function is called for every sample "
        "from every evaluation thread, and Python calls take turns on the GIL, so evaluate "
        "terrains with it on one thread (`threads=1`).")
        .def(nb::init<HeightFunction>(), "function"_a);
}

void BindRegions(nb::module_& m) {
    nb::class_<RegionShape>(m, "RegionShape",
                            "A rectangle, circle or polygon in surface coordinates.")
        .def_static(
            "rectangle",
            [](double minX, double minY, double maxX, double maxY) {
                return RegionShape::Rectangle({minX, minY, maxX, maxY});
            },
            "min_x"_a, "min_y"_a, "max_x"_a, "max_y"_a)
        .def_static(
            "circle",
            [](double x, double y, double radius) { return RegionShape::Circle({x, y}, radius); },
            "x"_a, "y"_a, "radius"_a)
        .def_static("polygon", &RegionShape::Polygon, "points"_a,
                    "holes"_a = std::vector<std::vector<Vec2d>>{},
                    "A polygon outlined by `points`, (x, y) pairs closed from the last back to the "
                    "first, less the polygons `holes`; concave outlines are allowed.")
        .def_ro("points", &RegionShape::points, "A polygon's outline; empty for other shapes.")
        .def_ro("holes", &RegionShape::holes, "A polygon's holes.")
        .def("distance_outside", &RegionShape::DistanceOutside, "x"_a, "y"_a,
             "surface"_a = Surface{},
             "Signed distance in meters to the shape's edge: positive outside, negative inside.");
    m.def("validate_region_shape", &ValidateRegionShape, "shape"_a, kValidateDoc);
    nb::class_<MaskedLayer, Layer>(
        m, "MaskedLayer",
        "`inner` applied only inside `shape`, faded out over `feather` meters outside it.")
        .def(
            "__init__",
            [](MaskedLayer* self, LayerPtr inner, RegionShape shape, double feather,
               BlendShape blend) {
                new (self) MaskedLayer(std::move(inner), std::move(shape), feather, blend);
            },
            "inner"_a, "shape"_a, "feather"_a = 0.0, "blend"_a = BlendShape::Smoothstep)
        .def("weight", &MaskedLayer::Weight, "x"_a, "y"_a, "surface"_a = Surface{},
             "The inner layer's weight at (x, y) on `surface`: 1 inside the shape, 0 from "
             "`feather` outside it, the blend between.")
        .def("holds", &MaskedLayer::Holds, "x"_a, "y"_a, "surface"_a = Surface{},
             "Whether a feature centered at (x, y) on `surface` belongs to the region: within it "
             "or its feather.")
        .def_prop_ro("inner", &MaskedLayer::Inner, nb::rv_policy::reference_internal,
                     "The layer applied inside the region.");
}

void BindRoughness(nb::module_& m) {
    BindOptions<RoughnessOptions>(
        m, "RoughnessOptions",
        "Fine roughness of `rms_slope` (m/m) over `baseline` meters with Hurst exponent `hurst`, "
        "between `min_wavelength` and `max_wavelength` meters.",
        {&ValidateRoughnessOptions, "validate_roughness_options", "options"});
    nb::class_<RoughnessLayer, Layer>(m, "RoughnessLayer", "Fine roughness of a given RMS slope.")
        .def(nb::init<RoughnessOptions>(), "options"_a = RoughnessOptions{})
        .def("rms_slope", &RoughnessLayer::RmsSlope, "baseline"_a, "surface"_a = Surface{})
        .def("wavelengths", &RoughnessLayer::Wavelengths,
             "The wavelengths (m) of the roughness octaves.")
        .def_prop_ro("options", &RoughnessLayer::Options);
    m.def("perlin_structure_function", &PerlinStructureFunction, "r"_a,
          "The structure function of 2D Perlin noise at lag `r` lattice units.");
    m.def("perlin3_structure_function", &Perlin3StructureFunction, "r"_a,
          "The structure function of 3D Perlin noise at lag `r` lattice units.");
}

void BindExposure(nb::module_& m) {
    BindOptions<ExposureParams>(
        m, "ExposureParams",
        "How exposed the ground is (crests and steep slopes shed regolith): rises and slopes "
        "for soil and boulder, read over `reach` meters.",
        {&ValidateExposureParams, "validate_exposure_params", "params"});
    auto exposureField =
        nb::class_<ExposureField>(m, "ExposureField",
                                  "How exposed the ground is, from 0 (buried under regolith) to 1 "
                                  "(bare boulder): crests and "
                                  "steep slopes shed regolith, read over `params.reach` meters.")
            .def(nb::init<ExposureParams, Surface>(), "params"_a = ExposureParams{},
                 "surface"_a = Surface{})
            .def(
                "fill",
                [](const ExposureField& field, const Ground& ground, const RasterGrid& grid,
                   double spacing, unsigned threads) {
                    Raster<float> exposure = grid.MakeRaster();
                    field.Fill(exposure, ground, spacing, threads);
                    return exposure;
                },
                "ground"_a, "grid"_a, "spacing"_a, "threads"_a = 0,
                nb::call_guard<nb::gil_scoped_release>(),
                "The exposure over `grid` of `ground`, such as `terrain.ground(n)` beneath layer "
                "n of a Terrain, sampled every `spacing` meters.");
    BindGroundQuery<ExposureField>(
        exposureField, "exposure",
        [](const ExposureField& exposure, double x, double y, const LayerContext& context) {
            RequireGround(context.ground, "Exposure fields");
            return exposure.Exposure(*context.ground, x, y, context.spacing);
        },
        "The exposure at (x, y), from 0 to 1.");
}

void BindBedforms(nb::module_& m) {
    BindOptions<BedformOptions>(
        m, "BedformOptions",
        "Ripples and dunes: `wavelength` and `height` (m), crests across `wind_azimuth_deg`"
        "(degrees "
        "counter-clockwise from +x), covering `cover` of the ground in fields `field_size` meters "
        "across.",
        {&ValidateBedformOptions, "validate_bedform_options", "options"});
    m.def("mars_large_ripples", &MarsLargeRipples, "Martian large ripples.");
    m.def("mars_transverse_aeolian_ridges", &MarsTransverseAeolianRidges,
          "Martian transverse aeolian ridges.");
    auto bedforms = nb::class_<BedformLayer, Layer>(m, "BedformLayer", "Aeolian bedforms.");
    bedforms.def(nb::init<BedformOptions>(), "options"_a)
        .def_prop_ro("options", &BedformLayer::Options);
    BindMaterialCover<BedformLayer>(bedforms);
}

void BindMaterialClasses(nb::module_& m) {
    nb::enum_<MaterialClass>(m, "MaterialClass", "What the surface is made of at a point.")
        .value("SOIL", MaterialClass::Soil)
        .value("SAND", MaterialClass::Sand)
        .value("DUST", MaterialClass::Dust)
        .value("BEDROCK", MaterialClass::Bedrock)
        .value("ROCK", MaterialClass::Rock);
    m.attr("MATERIAL_CLASSES") = kMaterialClasses;
}

void BindSandFill(nb::module_& m) {
    BindOptions<SandFillOptions>(
        m, "SandFillOptions",
        "Sand pooled in lows: `fill` of the depth below the surroundings `reach` meters around, up "
        "to `max_thickness` meters.",
        {&ValidateSandFillOptions, "validate_sand_fill_options", "options"});
    auto layer = nb::class_<SandFillLayer, Layer>(m, "SandFillLayer",
                                                  "Sand pooled in the lows of the ground beneath.");
    layer.def(nb::init<SandFillOptions>(), "options"_a = SandFillOptions{})
        .def_prop_ro("options", &SandFillLayer::Options);
    BindGroundQuery<SandFillLayer>(
        layer, "thickness_at",
        [](const SandFillLayer& fill, double x, double y, const LayerContext& context) {
            return fill.ThicknessAt(x, y, context);
        },
        "The sand thickness at (x, y) in meters.");
    BindMaterialCover<SandFillLayer>(layer);
}

void BindSpectrumFit(nb::module_& m) {
    auto band = nb::class_<SpectrumFitBand>(
        m, "SpectrumFitBand",
        "The wavelengths, in base pixels, a power-law spectrum is fitted over.");
    BindKwargsInit<SpectrumFitBand>(band);
    band.def_rw("min_px", &SpectrumFitBand::minPixels)
        .def_rw("max_px", &SpectrumFitBand::maxPixels)
        .def("validate", &ValidateSpectrumFitBand, kValidateDoc);
    m.def("validate_spectrum_fit_band", &ValidateSpectrumFitBand, "band"_a, kValidateDoc);
    nb::class_<PowerLawSpectrum>(m, "PowerLawSpectrum",
                                 "A power-law spectrum by its slope beta and the RMS height (m) it "
                                 "holds in the octave just finer than the base's Nyquist "
                                 "wavelength.")
        .def_ro("slope", &PowerLawSpectrum::slope)
        .def_ro("octave_rms", &PowerLawSpectrum::octaveRms)
        .def_ro("clipped", &PowerLawSpectrum::clipped,
                "Whether the fitted slope fell outside [2, 4] and was clamped to it.");
    m.def("estimate_power_law_spectrum", &EstimatePowerLawSpectrum, "heights"_a,
          "band"_a = SpectrumFitBand{}, "The power law fitted to a raster's radial spectrum.");
}

void BindSpectralContinuation(nb::module_& m) {
    BindSpectrumFit(m);
    BindOptions<SpectralContinuationOptions>(
        m, "SpectralContinuationOptions",
        "The power-law spectrum (`slope`, `octave_rms` meters) of a base of `pixel_size` meters "
        "that a spectral continuation layer extends down to `min_wavelength` meters.",
        {&ValidateSpectralContinuationOptions, "validate_spectral_continuation_options",
         "options"});
    nb::class_<SpectralContinuationLayer, Layer>(
        m, "SpectralContinuationLayer",
        "Detail finer than a base continuing its power-law spectrum, as sparse Gabor noise.")
        .def(nb::init<SpectralContinuationOptions>(), "options"_a)
        .def_prop_ro("options", &SpectralContinuationLayer::Options);
}

void BindRoughnessEstimate(nb::module_& m) {
    nb::class_<BaseRoughness>(m, "BaseRoughness",
                              "Self-affine roughness: an RMS height difference `deviation` meters "
                              "at a lag of `cell_size` meters that grows as lag^hurst.")
        .def(nb::init<>())
        .def_rw("hurst", &BaseRoughness::hurst)
        .def_rw("deviation", &BaseRoughness::deviation)
        .def_rw("cell_size", &BaseRoughness::cellSize)
        .def("deviation_at", &BaseRoughness::Deviation, "lag"_a,
             "The RMS height difference in meters at a lag in meters.")
        .def("rms_slope", &BaseRoughness::RmsSlope, "baseline"_a,
             "The RMS slope (rise over run) over a baseline in meters.");
    m.def("estimate_source_roughness", &EstimateSourceRoughness, "source"_a,
          "surface"_a = Surface{},
          "The roughness of a source on a lattice of its native cells about its grid's center.");
    m.def("matched_noise_amplitude", &MatchedNoiseAmplitude, "layer"_a, "roughness"_a,
          "The amplitude in meters that gives a noise layer the roughness's RMS height "
          "difference at a lag of its wavelength.");
}

void BindPavement(nb::module_& m) {
    BindOptions<PavementOptions>(
        m, "PavementOptions",
        "Fractured bedrock: Voronoi plates `plate_size` meters across, each tilted and stepped, "
        "parted by cracks (or raised ridges) `crack_width` meters wide and `crack_depth` deep; "
        "angles in degrees.",
        {&ValidatePavementOptions, "validate_pavement_options", "options"});
    m.def("mars_desiccation_ridges", &MarsDesiccationRidges,
          "The desiccation ridges of Gale crater's Old Soaker slab (Stein et al. 2018).");
    auto layer = nb::class_<PavementLayer, Layer>(m, "PavementLayer", "Fractured bedrock plates.");
    layer.def(nb::init<PavementOptions>(), "options"_a)
        .def_prop_ro("options", &PavementLayer::Options);
    BindMaterialCover<PavementLayer>(layer);
}

void BindStrata(nb::module_& m) {
    BindOptions<StrataOptions>(
        m, "StrataOptions",
        "Layered rock: beds `bed_thickness` meters thick, dipping `dip_deg` toward "
        "`dip_azimuth_deg`, each "
        "cut into a flat bench and a cliff by `sharpness`; angles in degrees.",
        {&ValidateStrataOptions, "validate_strata_options", "options"});
    auto layer = nb::class_<StrataLayer, Layer>(m, "StrataLayer",
                                                "Layered rock terraced into benches and cliffs.");
    layer.def(nb::init<StrataOptions>(), "options"_a).def_prop_ro("options", &StrataLayer::Options);
    BindMaterialCover<StrataLayer>(layer);
}

void BindDuneParts(nb::module_& m) {
    BindEnum<DuneShape>(m, "DuneShape", "The kind of dune a DuneLayer builds.");
    BindOptions<DuneRipples>(
        m, "DuneRipples",
        "Ripples on dunes: `wavelength` and `height` (m) on flat sand, turned and tightened by the "
        "dune's slope by `slope_coupling`; DuneOptions.ripples None gives none.");
    BindOptions<DuneGrainflows>(
        m, "DuneGrainflows",
        "Grainflows on slip faces: scarps and lobes with sizes (m) uniform between their bounds, "
        "`thickness` meters deep and high, in `cover` of the lattice cells; DuneOptions.grainflows "
        "None "
        "gives none.");
    m.def("mars_dune_grainflows", &MarsDuneGrainflows,
          "The grainflows of Namib Dune's lee slope (Ewing et al. 2017).");
}

void BindDunes(nb::module_& m) {
    BindDuneParts(m);
    BindOptions<DuneOptions>(
        m, "DuneOptions",
        "Dunes: barchans and domes of `width` (m) in cells `spacing` meters apart, or crest-line "
        "dunes `wavelength` meters apart and `height` meters high; angles in degrees, the wind "
        "counter-clockwise from +x.",
        {&ValidateDuneOptions, "validate_dune_options", "options"});
    m.def("mars_dune_ripples", &MarsDuneRipples, "Martian large wind ripples to lay on dunes.");
    m.def("mars_barchans", &MarsBarchans, "Martian barchans with large ripples.");
    m.def("mars_transverse_dunes", &MarsTransverseDunes,
          "The transverse dunes of Mars's north polar erg, with large ripples.");
    auto dunes =
        nb::class_<DuneLayer, Layer>(m, "DuneLayer", "Wind-blown dunes, with ripples on them.");
    dunes.def(nb::init<DuneOptions>(), "options"_a)
        .def("dune_height_at", &DuneLayer::DuneHeightAt, "x"_a, "y"_a, "surface"_a = Surface{},
             "The dunes' height at (x, y) on `surface` in meters, without ripples.")
        .def_prop_ro("options", &DuneLayer::Options);
    BindMaterialCover<DuneLayer>(dunes);
}

void BindDensityMaps(nb::module_& m) {
    BindEnum<DensityMeasure>(m, "DensityMeasure", "What a density map's values measure.");
    auto map = nb::class_<DensityMap>(
        m, "DensityMap",
        "A map of a crater or boulder population that sets a layer's density point by point: "
        "its `field` measures (`measure`) a scale, a count per `area_unit` square meters or an "
        "areal fraction of features from `min_diameter` to `max_diameter` meters. With `total` it "
        "counts every feature of its kind, and the secondaries thrown from a crater layer and the "
        "boulders a layer throws out around craters are taken out of it first.");
    BindKwargsInit<DensityMap>(map);
    map.def_rw("field", &DensityMap::field)
        .def_rw("measure", &DensityMap::measure)
        .def_rw("min_diameter", &DensityMap::minDiameter)
        .def_rw("max_diameter", &DensityMap::maxDiameter)
        .def_rw("area_unit", &DensityMap::areaUnit)
        .def_rw("total", &DensityMap::total)
        .def("validate", &ValidateDensityMap, kValidateDoc);
    m.def("validate_density_map", &ValidateDensityMap, "map"_a, kValidateDoc);
    m.def("map_measure", &MapMeasure, "map"_a, "distribution"_a,
          "What `map` measures of `distribution` as it is.");
    nb::class_<DensityScale>(m, "DensityScale",
                             "The local scale a density map gives a size distribution.")
        .def(nb::init<DensityMap, const SizeFrequencyDistribution&>(), "map"_a, "distribution"_a)
        .def("at", &DensityScale::At, "x"_a, "y"_a)
        .def("scale", &DensityScale::Scale, "value"_a)
        .def_prop_ro("measure", &DensityScale::Measure)
        .def_prop_ro("max", &DensityScale::Max);
}

void BindSizeSlopeMaps(nb::module_& m) {
    BindEnum<SizeSlopeMeasure>(m, "SizeSlopeMeasure", "What a size slope map's values measure.");
    auto map = nb::class_<SizeSlopeMap>(
        m, "SizeSlopeMap",
        "A map of how steeply feature counts fall with size: its `field` gives the cumulative "
        "exponent alpha, or the mean diameter from `reference_diameter` up, and the sizes become "
        "a local power law through their count at `reference_diameter`, alpha clamped to "
        "[`min_exponent`, `max_exponent`].");
    BindKwargsInit<SizeSlopeMap>(map);
    map.def_rw("field", &SizeSlopeMap::field)
        .def_rw("measure", &SizeSlopeMap::measure)
        .def_rw("reference_diameter", &SizeSlopeMap::referenceDiameter)
        .def_rw("min_exponent", &SizeSlopeMap::minExponent)
        .def_rw("max_exponent", &SizeSlopeMap::maxExponent)
        .def("validate", &ValidateSizeSlopeMap, kValidateDoc);
    m.def("validate_size_slope_map", &ValidateSizeSlopeMap, "map"_a, kValidateDoc);
    m.def("size_slope_at", &SizeSlopeAt, "map"_a, "x"_a, "y"_a,
          "The exponent the map gives at (x, y), clamped; None where it has no value.");
}

}  // namespace

void BindLayers(nb::module_& m) {
    BindDensityMaps(m);
    BindSizeSlopeMaps(m);
    BindLayerBase(m);
    BindFades(m);
    BindTransforms(m);
    BindRegions(m);
    BindRoughness(m);
    BindExposure(m);
    BindBedforms(m);
    BindDunes(m);
    BindMaterialClasses(m);
    BindSandFill(m);
    BindSpectralContinuation(m);
    BindRoughnessEstimate(m);
    BindPavement(m);
    BindStrata(m);
}

}  // namespace terraingen::python
