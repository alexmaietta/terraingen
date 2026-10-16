#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "trampolines.h"

#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/region.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/transforms.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief A layer written in Python: `apply_at(height, x, y, context)` changes one height,
/// `apply(heights, context)` a whole raster at once (faster with NumPy); either is enough.
/// `max_feature_diameter()` says how far, in metres, the layer's features reach, so tiles get
/// their neighbours' features.
class PyLayer : public Layer {
  public:
    NB_TRAMPOLINE(Layer);

    double MaxFeatureDiameter() const override {
        NB_OVERRIDE_NAME("max_feature_diameter", MaxFeatureDiameter);
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
            throw std::logic_error("A Python Layer must define apply_at or apply");
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
        "or `apply(heights, context)` (a Raster changed in place), and optionally "
        "`max_feature_diameter()`, for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Layer& layer, Raster<float>& heights, const LayerContext& context) {
                layer.Apply(heights, context);
            },
            "heights"_a, "context"_a)
        .def("apply_at", &Layer::ApplyAt, "height"_a, "x"_a, "y"_a, "context"_a)
        .def("max_feature_diameter", &Layer::MaxFeatureDiameter);
}

void BindFades(nb::module_& m) {
    nb::enum_<BlendShape>(m, "BlendShape", "How a weight falls from 1 to 0.")
        .value("LINEAR", BlendShape::Linear)
        .value("SMOOTHSTEP", BlendShape::Smoothstep)
        .value("SMOOTHERSTEP", BlendShape::Smootherstep)
        .value("GAUSSIAN", BlendShape::Gaussian);
    auto fade = nb::class_<ResolutionFade>(
        m, "ResolutionFade",
        "How features too small to resolve fade out, by how many samples span them: absent up "
        "to `start_samples`, whole from `end_samples`.");
    BindKwargsInit<ResolutionFade>(fade);
    fade.def_rw("start_samples", &ResolutionFade::startSamples)
        .def_rw("end_samples", &ResolutionFade::endSamples)
        .def_rw("shape", &ResolutionFade::shape);
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
               const Surface& surface) {
                new (self) NoiseRelief(SyntheticTerrainParams{std::move(layers)}, origin, surface);
            },
            "layers"_a, "origin"_a = Vec2d{}, "surface"_a = Surface{},
            "On a sphere the noise is 3D at each point's position, seamless everywhere, and "
            "`origin` is unused.");
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
    nb::class_<RegionLayer, Layer>(
        m, "RegionLayer",
        "`layer` applied only inside `shape`, faded out over `feather` metres outside it.")
        .def(nb::init<std::shared_ptr<Layer>, RegionShape, double, BlendShape, Surface>(),
             "layer"_a, "shape"_a, "feather"_a = 0.0, "blend"_a = BlendShape::Smoothstep,
             "surface"_a = Surface{})
        .def("weight", &RegionLayer::Weight, "x"_a, "y"_a)
        .def_prop_ro("inner", &RegionLayer::Inner, nb::rv_policy::reference_internal,
                     "The layer applied inside the region.");
}

void BindRoughness(nb::module_& m) {
    auto options = nb::class_<RoughnessOptions>(
        m, "RoughnessOptions",
        "Fine roughness of `rms_slope` (m/m) over `baseline` metres with Hurst exponent `hurst`, "
        "between `min_wavelength` and `max_wavelength` metres.");
    BindKwargsInit<RoughnessOptions>(options);
    options.def_rw("rms_slope", &RoughnessOptions::rmsSlope)
        .def_rw("baseline", &RoughnessOptions::baseline)
        .def_rw("hurst", &RoughnessOptions::hurst)
        .def_rw("max_wavelength", &RoughnessOptions::maxWavelength)
        .def_rw("min_wavelength", &RoughnessOptions::minWavelength)
        .def_rw("seed", &RoughnessOptions::seed)
        .def_rw("fade", &RoughnessOptions::fade);
    m.def("validate_roughness_options", &ValidateRoughnessOptions, "options"_a, kValidateDoc);
    nb::class_<RoughnessLayer, Layer>(m, "RoughnessLayer", "Fine roughness of a given RMS slope.")
        .def(nb::init<RoughnessOptions, Surface>(), "options"_a = RoughnessOptions{},
             "surface"_a = Surface{})
        .def("rms_slope", &RoughnessLayer::RmsSlope, "baseline"_a)
        .def("wavelengths", &RoughnessLayer::Wavelengths,
             "The wavelengths (m) of the roughness octaves.");
    m.def("perlin_structure_function", &PerlinStructureFunction, "r"_a,
          "The structure function of 2D Perlin noise at lag `r` lattice units.");
    m.def("perlin3_structure_function", &Perlin3StructureFunction, "r"_a,
          "The structure function of 3D Perlin noise at lag `r` lattice units.");
}

void BindExposureParams(nb::module_& m) {
    auto params = nb::class_<ExposureParams>(
        m, "ExposureParams",
        "How exposed the ground is (crests and steep slopes shed regolith): rises and slopes "
        "for soil and boulder, read over `reach` metres.");
    BindKwargsInit<ExposureParams>(params);
    params.def_rw("reach", &ExposureParams::reach)
        .def_rw("rise_soil", &ExposureParams::riseSoil)
        .def_rw("rise_boulder", &ExposureParams::riseBoulder)
        .def_rw("slope_soil", &ExposureParams::slopeSoil)
        .def_rw("slope_boulder", &ExposureParams::slopeBoulder)
        .def_rw("floor_cover", &ExposureParams::floorCover)
        .def_rw("floor_patch", &ExposureParams::floorPatch)
        .def_rw("seed", &ExposureParams::seed);
    m.def("validate_exposure_params", &ValidateExposureParams, "params"_a, kValidateDoc);
}

void BindExposureField(nb::module_& m) {
    nb::class_<ExposureField>(
        m, "ExposureField",
        "How exposed the ground is, from 0 (buried under regolith) to 1 (bare boulder): crests and "
        "steep slopes shed regolith, read over `params.reach` metres.")
        .def(nb::init<ExposureParams, Surface>(), "params"_a = ExposureParams{},
             "surface"_a = Surface{})
        .def(
            "fill",
            [](const ExposureField& field, const PyTerrain& terrain, std::size_t layers,
               const RasterGrid& grid, double spacing, unsigned threads) {
                Raster<float> exposure = grid.MakeRaster();
                WithGround(&terrain, layers, [&](const Ground* ground) {
                    field.Fill(exposure, *ground, spacing, threads);
                });
                return exposure;
            },
            "terrain"_a, "layers"_a, "grid"_a, "spacing"_a, "threads"_a = 0,
            nb::call_guard<nb::gil_scoped_release>(),
            "The exposure over `grid` of the ground made by `terrain`'s source and its first "
            "`layers` layers, sampled every `spacing` metres.")
        .def(
            "exposure",
            [](const ExposureField& field, const PyTerrain& terrain, std::size_t layers, double x,
               double y, double spacing) {
                return WithGround(&terrain, layers, [&](const Ground* ground) {
                    return field.Exposure(*ground, x, y, spacing);
                });
            },
            "terrain"_a, "layers"_a, "x"_a, "y"_a, "spacing"_a);
}

void BindBedformOptions(nb::module_& m) {
    auto options = nb::class_<BedformOptions>(
        m, "BedformOptions",
        "Ripples and dunes: `wavelength` and `height` (m), crests across `wind_azimuth` (degrees "
        "from north), covering `cover` of the ground in fields `field_size` metres across.");
    BindKwargsInit<BedformOptions>(options);
    options.def_rw("wavelength", &BedformOptions::wavelength)
        .def_rw("height", &BedformOptions::height)
        .def_rw("wind_azimuth", &BedformOptions::windAzimuth)
        .def_rw("azimuth_spread", &BedformOptions::azimuthSpread)
        .def_rw("lee_fraction", &BedformOptions::leeFraction)
        .def_rw("sinuosity", &BedformOptions::sinuosity)
        .def_rw("cover", &BedformOptions::cover)
        .def_rw("field_size", &BedformOptions::fieldSize)
        .def_rw("seed", &BedformOptions::seed)
        .def_rw("exposure", &BedformOptions::exposure)
        .def_rw("exposure_spacing", &BedformOptions::exposureSpacing)
        .def_rw("fade", &BedformOptions::fade);
    m.def("validate_bedform_options", &ValidateBedformOptions, "options"_a, kValidateDoc);
    m.def("mars_large_ripples", &MarsLargeRipples, "Martian large ripples.");
    m.def("mars_transverse_aeolian_ridges", &MarsTransverseAeolianRidges,
          "Martian transverse aeolian ridges.");
}

void BindBedforms(nb::module_& m) {
    BindBedformOptions(m);
    nb::class_<BedformLayer, Layer>(m, "BedformLayer", "Aeolian bedforms.")
        .def(nb::init<BedformOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def(
            "cover_at",
            [](const BedformLayer& layer, double x, double y, const PyTerrain* terrain,
               std::size_t layers) {
                return WithGround(terrain, layers, [&](const Ground* ground) {
                    return layer.CoverAt(x, y, ground);
                });
            },
            "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
            "The share of (x, y) covered by bedforms; bedforms that read the exposure field need "
            "the ground beneath them: `terrain`'s source and its first `layers` layers.");
}

}  // namespace

void BindLayers(nb::module_& m) {
    BindLayerBase(m);
    BindFades(m);
    BindTransforms(m);
    BindRegions(m);
    BindRoughness(m);
    BindExposureParams(m);
    BindExposureField(m);
    BindBedforms(m);
}

}  // namespace terraingen::python
