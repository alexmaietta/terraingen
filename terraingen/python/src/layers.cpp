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
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/roughness_estimate.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/layers/strata.h"
#include "terraingen/layers/transforms.h"

#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief A layer written in Python: `apply_at(height, x, y, context)` changes one height,
/// `apply(heights, context)` a whole raster at once (faster with NumPy); either is enough.
class PyLayer : public Layer {
  public:
    NB_TRAMPOLINE(Layer);

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
    /// `apply` on a raster of one cell of side `context.spacing` centred at `p`.
    float ApplyToOneCell(float height, const Vec2d& p, const LayerContext& context) const {
        Raster<float> cell(1, 1, height);
        cell.SetTransform(GeoTransform::NorthUp(p.x - 0.5 * context.spacing,
                                                p.y + 0.5 * context.spacing, context.spacing));
        CallOverrideIgnoringResult(nb_trampoline, "apply", ByReference<Raster<float>>{cell},
                                   context);
        return cell(0, 0);
    }
};

void BindLayerBase(nb::module_& m) {
    nb::class_<LayerContext>(m, "LayerContext",
                             "How a layer is evaluated: the sample spacing (m) and threads.")
        .def(
            "__init__",
            [](LayerContext* self, double spacing, unsigned threads) {
                new (self) LayerContext{spacing, threads, nullptr};
            },
            "spacing"_a = 0.0, "threads"_a = 1)
        .def_ro("spacing", &LayerContext::spacing)
        .def_ro("threads", &LayerContext::threads);
    nb::class_<Layer, PyLayer>(
        m, "Layer",
        "Relief added to a terrain. Subclass it and implement `apply_at(height, x, y, context)` "
        "or `apply(heights, context)` (a Raster changed in place), for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Layer& layer, Raster<float>& heights, const LayerContext& context) {
                layer.Apply(heights, context);
            },
            "heights"_a, "context"_a)
        .def("apply_at", &Layer::ApplyAt, "height"_a, "x"_a, "y"_a, "context"_a);
}

void BindFades(nb::module_& m) {
    BindEnum<BlendShape>(m, "BlendShape", "How a weight falls from 1 to 0.");
    BindOptions<ResolutionFade>(
        m, "ResolutionFade",
        "How features too small to resolve fade out, by how many samples span them: absent up "
        "to `start_samples`, whole from `end_samples`.",
        &ValidateResolutionFade);
    m.def("validate_resolution_fade", &ValidateResolutionFade, "fade"_a, kValidateDoc);
    m.def("falloff", &Falloff, "x"_a, "start"_a, "end"_a, "shape"_a,
          "The weight at `x` of a falloff from 1 at `start` to 0 at `end` along `shape`.");
    m.def("smoothstep", &Smoothstep, "t"_a, "The cubic smoothstep of `t` clamped to [0, 1].");
    m.def("resolution_weight", &ResolutionWeight, "diameter"_a, "spacing"_a, "fade"_a,
          "The weight of a feature `diameter` across sampled every `spacing`.");
}

void BindTransforms(nb::module_& m) {
    nb::class_<NoiseRelief, Layer>(m, "NoiseRelief",
                                   "A stack of 1 to 8 noise layers added to the terrain, measured "
                                   "from `origin`.")
        .def(
            "__init__",
            [](NoiseRelief* self, std::vector<NoiseLayer> layers, const Vec2d& origin,
               const Surface& surface, std::shared_ptr<const Source> source,
               std::optional<double> sourceResolvesPixels) {
                new (self) NoiseRelief(SyntheticTerrainParams{std::move(layers)}, origin, surface,
                                       std::move(source), sourceResolvesPixels);
            },
            "layers"_a, "origin"_a = Vec2d{}, "surface"_a = Surface{}, "source"_a = nb::none(),
            "source_resolves_px"_a = nb::none(),
            "On a sphere the noise is 3D at each point's position, seamless everywhere, and "
            "`origin` is unused. With a `source` and `source_resolves_px`, each layer whose "
            "wavelength is at least that many of the source's cells at a point is left out there.")
        .def_prop_ro(
            "layers", [](const NoiseRelief& relief) { return relief.Params().layers; },
            "The noise layers, with any Hurst exponents and amplitudes applied.")
        .def_prop_ro("source_resolves_px", &NoiseRelief::SourceResolvesPixels,
                     "Source cells across from which the source holds a noise layer; None for "
                     "none.");
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
        .def(nb::init<HeightFunction, Surface>(), "function"_a, "surface"_a = Surface{});
}

void BindRegions(nb::module_& m) {
    nb::class_<RegionShape>(m, "RegionShape", "A rectangle or circle in surface coordinates.")
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
        .def("distance_outside", &RegionShape::DistanceOutside, "x"_a, "y"_a,
             "surface"_a = Surface{});
    m.def("validate_region_shape", &ValidateRegionShape, "shape"_a, kValidateDoc);
    nb::class_<MaskedLayer, Layer>(
        m, "MaskedLayer",
        "`inner` applied only inside `shape`, faded out over `feather` meters outside it.")
        .def(nb::init<std::shared_ptr<Layer>, RegionShape, double, BlendShape, Surface>(),
             "inner"_a, "shape"_a, "feather"_a = 0.0, "blend"_a = BlendShape::Smoothstep,
             "surface"_a = Surface{})
        .def("weight", &MaskedLayer::Weight, "x"_a, "y"_a)
        .def_prop_ro("inner", &MaskedLayer::Inner, nb::rv_policy::reference_internal,
                     "The layer applied inside the region.");
}

void BindRoughness(nb::module_& m) {
    BindOptions<RoughnessOptions>(
        m, "RoughnessOptions",
        "Fine roughness of `rms_slope` (m/m) over `baseline` meters with Hurst exponent `hurst`, "
        "between `min_wavelength` and `max_wavelength` meters.",
        &ValidateRoughnessOptions);
    m.def("validate_roughness_options", &ValidateRoughnessOptions, "options"_a, kValidateDoc);
    nb::class_<RoughnessLayer, Layer>(m, "RoughnessLayer", "Fine roughness of a given RMS slope.")
        .def(nb::init<RoughnessOptions, Surface>(), "options"_a = RoughnessOptions{},
             "surface"_a = Surface{})
        .def("rms_slope", &RoughnessLayer::RmsSlope, "baseline"_a)
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
        &ValidateExposureParams);
    m.def("validate_exposure_params", &ValidateExposureParams, "params"_a, kValidateDoc);
    auto exposureField =
        nb::class_<ExposureField>(m, "ExposureField",
                                  "How exposed the ground is, from 0 (buried under regolith) to 1 "
                                  "(bare boulder): crests and "
                                  "steep slopes shed regolith, read over `params.reach` meters.")
            .def(nb::init<ExposureParams, Surface>(), "params"_a = ExposureParams{},
                 "surface"_a = Surface{})
            .def(
                "fill",
                [](const ExposureField& field, const PyTerrainGenerator& generator,
                   std::size_t layers, const RasterGrid& grid, double spacing, unsigned threads) {
                    Raster<float> exposure = grid.MakeRaster();
                    WithGround(&generator, layers, [&](const Ground* ground) {
                        field.Fill(exposure, *ground, spacing, threads);
                    });
                    return exposure;
                },
                "generator"_a, "layers"_a, "grid"_a, "spacing"_a, "threads"_a = 0,
                nb::call_guard<nb::gil_scoped_release>(),
                "The exposure over `grid` of the ground made by `generator`'s source and its first "
                "`layers` layers, sampled every `spacing` meters.");
    BindGroundQuery<ExposureField>(
        exposureField, "exposure",
        [](const ExposureField& exposure, double x, double y, double spacing,
           const Ground* ground) {
            RequireGround(ground, "Exposure fields");
            return exposure.Exposure(*ground, x, y, spacing);
        },
        "The exposure at (x, y), from 0 to 1.");
}

void BindBedforms(nb::module_& m) {
    BindOptions<BedformOptions>(
        m, "BedformOptions",
        "Ripples and dunes: `wavelength` and `height` (m), crests across `wind_azimuth_deg` "
        "(degrees "
        "counter-clockwise from +x), covering `cover` of the ground in fields `field_size` meters "
        "across.",
        &ValidateBedformOptions);
    m.def("validate_bedform_options", &ValidateBedformOptions, "options"_a, kValidateDoc);
    m.def("mars_large_ripples", &MarsLargeRipples, "Martian large ripples.");
    m.def("mars_transverse_aeolian_ridges", &MarsTransverseAeolianRidges,
          "Martian transverse aeolian ridges.");
    auto bedforms = nb::class_<BedformLayer, Layer>(m, "BedformLayer", "Aeolian bedforms.");
    bedforms
        .def(nb::init<BedformOptions, Surface, std::shared_ptr<const Source>>(), "options"_a,
             "surface"_a = Surface{}, "source"_a = nb::none())
        .def(
            "cover_at",
            [](const BedformLayer& layer, double x, double y, const PyTerrainGenerator* generator,
               std::size_t layers) {
                return WithGround(generator, layers, [&](const Ground* ground) {
                    return layer.CoverAt(x, y, ground);
                });
            },
            "x"_a, "y"_a, "generator"_a = nb::none(), "layers"_a = 0,
            "The share of (x, y) covered by bedforms; bedforms that read the exposure field need "
            "the ground beneath them: `generator`'s source and its first `layers` layers.");
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
        &ValidateSandFillOptions);
    m.def("validate_sand_fill_options", &ValidateSandFillOptions, "options"_a, kValidateDoc);
    auto layer = nb::class_<SandFillLayer, Layer>(m, "SandFillLayer",
                                                  "Sand pooled in the lows of the ground beneath.");
    layer
        .def(nb::init<SandFillOptions, Surface>(), "options"_a = SandFillOptions{},
             "surface"_a = Surface{})
        .def_prop_ro("options", &SandFillLayer::Options);
    BindGroundQuery<SandFillLayer>(
        layer, "thickness_at",
        [](const SandFillLayer& fill, double x, double y, double spacing, const Ground* ground) {
            RequireGround(ground, "Sand fill layers");
            return fill.ThicknessAt(*ground, x, y, spacing);
        },
        "The sand thickness at (x, y) in meters.");
    BindMaterialCover<SandFillLayer>(layer);
}

void BindSpectrumFit(nb::module_& m) {
    nb::class_<SpectrumBin>(m, "SpectrumBin", "One annulus of a radially averaged power spectrum.")
        .def_ro("frequency", &SpectrumBin::frequency, "Mean radial frequency, cycles per unit.")
        .def_ro("power", &SpectrumBin::power, "Mean power spectral density.")
        .def_ro("count", &SpectrumBin::count, "Frequency samples averaged.");
    m.def("radial_power_spectrum", &RadialPowerSpectrum, "heights"_a,
          "The radially averaged power spectral density of a raster on its own cells, Hann "
          "windowed and detrended.");
    auto band = nb::class_<SpectrumFitBand>(
        m, "SpectrumFitBand",
        "The wavelengths, in base pixels, a power-law spectrum is fitted over.");
    BindKwargsInit<SpectrumFitBand>(band);
    band.def_rw("min_pixels", &SpectrumFitBand::minPixels)
        .def_rw("max_pixels", &SpectrumFitBand::maxPixels)
        .def("validate", &ValidateSpectrumFitBand, kValidateDoc);
    m.def("validate_spectrum_fit_band", &ValidateSpectrumFitBand, "band"_a, kValidateDoc);
    nb::class_<PowerLawSpectrum>(m, "PowerLawSpectrum",
                                 "A power-law spectrum by its slope beta and the RMS height (m) it "
                                 "holds in the octave just finer than the base's Nyquist "
                                 "wavelength.")
        .def_ro("slope", &PowerLawSpectrum::slope)
        .def_ro("octave_rms", &PowerLawSpectrum::octaveRms);
    m.def("estimate_power_law_spectrum", &EstimatePowerLawSpectrum, "heights"_a,
          "band"_a = SpectrumFitBand{}, "The power law fitted to a raster's radial spectrum.");
}

void BindSpectralContinuation(nb::module_& m) {
    BindSpectrumFit(m);
    BindOptions<SpectralContinuationOptions>(
        m, "SpectralContinuationOptions",
        "The power-law spectrum (`slope`, `octave_rms` meters) of a base of `pixel_size` meters "
        "that a spectral continuation layer extends down to `min_wavelength` meters.",
        &ValidateSpectralContinuationOptions);
    m.def("validate_spectral_continuation_options", &ValidateSpectralContinuationOptions,
          "options"_a, kValidateDoc);
    nb::class_<SpectralContinuationLayer, Layer>(
        m, "SpectralContinuationLayer",
        "Detail finer than a base continuing its power-law spectrum, as sparse Gabor noise.")
        .def(nb::init<SpectralContinuationOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("target_psd", &SpectralContinuationLayer::TargetPsd, "frequency"_a,
             "The target PSD in m^4 at a frequency in cycles per meter.")
        .def("band_frequencies", &SpectralContinuationLayer::BandFrequencies,
             "The lowest frequencies of the bands, in cycles per meter.")
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
    m.def("estimate_base_roughness", &EstimateBaseRoughness, "heights"_a, "cell_size"_a,
          "The Hurst exponent and RMS height difference of a raster's heights by the variogram "
          "method, at lags of 1 to 8 cells of `cell_size` meters.");
    m.def("estimate_source_roughness", &EstimateSourceRoughness, "source"_a,
          "surface"_a = Surface{},
          "The roughness of a source on a lattice of its native cells about its grid's centre.");
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
        &ValidatePavementOptions);
    m.def("validate_pavement_options", &ValidatePavementOptions, "options"_a, kValidateDoc);
    m.def("mars_desiccation_ridges", &MarsDesiccationRidges,
          "The desiccation ridges of Gale crater's Old Soaker slab (Stein et al. 2018).");
    auto layer = nb::class_<PavementLayer, Layer>(m, "PavementLayer", "Fractured bedrock plates.");
    layer.def(nb::init<PavementOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("relief_at", &PavementLayer::ReliefAt, "x"_a, "y"_a,
             "The pavement's relief at (x, y) in meters, before exposure and fading.")
        .def("edge_distance_at", &PavementLayer::EdgeDistanceAt, "x"_a, "y"_a,
             "The distance from (x, y) to the nearest plate edge, in meters.")
        .def_prop_ro("options", &PavementLayer::Options);
    BindMaterialCover<PavementLayer>(layer);
}

void BindStrata(nb::module_& m) {
    BindOptions<StrataOptions>(
        m, "StrataOptions",
        "Layered rock: beds `bed_thickness` meters thick, dipping `dip_deg` toward "
        "`dip_azimuth_deg`, each "
        "cut into a flat bench and a cliff by `sharpness`; angles in degrees.",
        &ValidateStrataOptions);
    m.def("validate_strata_options", &ValidateStrataOptions, "options"_a, kValidateDoc);
    auto layer = nb::class_<StrataLayer, Layer>(m, "StrataLayer",
                                                "Layered rock terraced into benches and cliffs.");
    layer.def(nb::init<StrataOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("terraced_at", &StrataLayer::TerracedAt, "height"_a, "x"_a, "y"_a,
             "The terraced height of ground `height` meters high at (x, y).")
        .def("bed_position_at", &StrataLayer::BedPositionAt, "height"_a, "x"_a, "y"_a,
             "How far through its bed ground `height` meters high at (x, y) lies, in [0, 1].")
        .def_prop_ro("options", &StrataLayer::Options);
    BindMaterialCover<StrataLayer>(layer);
}

void BindDuneParts(nb::module_& m) {
    BindEnum<DuneShape>(m, "DuneShape", "The kind of dune a DuneLayer builds.");
    BindOptions<DuneRipples>(
        m, "DuneRipples",
        "Ripples on dunes: `wavelength` and `height` (m) on flat sand, turned and tightened by the "
        "dune's slope by `slope_coupling`; a wavelength of 0 gives none.");
    BindOptions<DuneGrainflows>(
        m, "DuneGrainflows",
        "Grainflows on slip faces: scarps and lobes with sizes (m) uniform between their bounds, "
        "`thickness` meters deep and high, in `cover` of the lattice cells; 0 cover gives none.");
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
        &ValidateDuneOptions);
    m.def("validate_dune_options", &ValidateDuneOptions, "options"_a, kValidateDoc);
    m.def("mars_dune_ripples", &MarsDuneRipples, "Martian large wind ripples to lay on dunes.");
    m.def("mars_barchans", &MarsBarchans, "Martian barchans with large ripples.");
    m.def("mars_transverse_dunes", &MarsTransverseDunes,
          "The transverse dunes of Mars's north polar erg, with large ripples.");
    auto dunes =
        nb::class_<DuneLayer, Layer>(m, "DuneLayer", "Wind-blown dunes, with ripples on them.");
    dunes
        .def(nb::init<DuneOptions, Surface, std::shared_ptr<const Source>>(), "options"_a,
             "surface"_a = Surface{}, "source"_a = nb::none())
        .def("dune_height_at", &DuneLayer::DuneHeightAt, "x"_a, "y"_a,
             "The dunes' height at (x, y) in meters, without ripples.")
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
