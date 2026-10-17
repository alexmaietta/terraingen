#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "trampolines.h"

#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/detail.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/region.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/strata.h"
#include "terraingen/layers/transforms.h"

#include <stdexcept>
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
    nb::class_<DetailLayer, Layer>(
        m, "DetailLayer",
        "Adds `weight` times the detail of `full` above `smooth` at each sample, such as a DEM's "
        "detail above its own low-pass.")
        .def(
            "__init__",
            [](DetailLayer* self, std::shared_ptr<Source> full, std::shared_ptr<Source> smooth,
               double weight, const Surface& surface) {
                new (self) DetailLayer(std::move(full), std::move(smooth), weight, surface);
            },
            "full"_a, "smooth"_a, "weight"_a = 1.0, "surface"_a = Surface{});
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
    auto bedforms = nb::class_<BedformLayer, Layer>(m, "BedformLayer", "Aeolian bedforms.");
    bedforms.def(nb::init<BedformOptions, Surface>(), "options"_a, "surface"_a = Surface{})
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
    auto options = nb::class_<SandFillOptions>(
        m, "SandFillOptions",
        "Sand pooled in lows: `fill` of the depth below the surroundings `reach` metres around, up "
        "to `max_thickness` metres.");
    BindKwargsInit<SandFillOptions>(options);
    options.def_rw("reach", &SandFillOptions::reach)
        .def_rw("fill", &SandFillOptions::fill)
        .def_rw("max_thickness", &SandFillOptions::maxThickness)
        .def_rw("cover_thickness", &SandFillOptions::coverThickness);
    m.def("validate_sand_fill_options", &ValidateSandFillOptions, "options"_a, kValidateDoc);
    auto layer = nb::class_<SandFillLayer, Layer>(m, "SandFillLayer",
                                                  "Sand pooled in the lows of the ground beneath.");
    layer
        .def(nb::init<SandFillOptions, Surface>(), "options"_a = SandFillOptions{},
             "surface"_a = Surface{})
        .def(
            "thickness_at",
            [](const SandFillLayer& fill, double x, double y, double spacing,
               const PyTerrain& terrain, std::size_t layers) {
                return WithGround(&terrain, layers, [&](const Ground* ground) {
                    return fill.ThicknessAt(*ground, x, y, spacing);
                });
            },
            "x"_a, "y"_a, "spacing"_a, "terrain"_a, "layers"_a,
            "The sand thickness at (x, y) in metres over `terrain`'s source and its first "
            "`layers` layers.")
        .def_prop_ro("options", &SandFillLayer::Options);
    BindMaterialCover<SandFillLayer>(layer);
}

void BindPavement(nb::module_& m) {
    auto options = nb::class_<PavementOptions>(
        m, "PavementOptions",
        "Fractured bedrock: Voronoi plates `plate_size` metres across, each tilted and stepped, "
        "parted by cracks (or raised ridges) `crack_width` metres wide and `crack_depth` deep; "
        "angles in radians.");
    BindKwargsInit<PavementOptions>(options);
    options.def_rw("plate_size", &PavementOptions::plateSize)
        .def_rw("jitter", &PavementOptions::jitter)
        .def_rw("warp", &PavementOptions::warp)
        .def_rw("crack_width", &PavementOptions::crackWidth)
        .def_rw("crack_depth", &PavementOptions::crackDepth)
        .def_rw("raised", &PavementOptions::raised)
        .def_rw("edge_rounding", &PavementOptions::edgeRounding)
        .def_rw("max_tilt", &PavementOptions::maxTilt)
        .def_rw("max_step", &PavementOptions::maxStep)
        .def_rw("octaves", &PavementOptions::octaves)
        .def_rw("lacunarity", &PavementOptions::lacunarity)
        .def_rw("gain", &PavementOptions::gain)
        .def_rw("seed", &PavementOptions::seed)
        .def_rw("exposure", &PavementOptions::exposure)
        .def_rw("exposure_spacing", &PavementOptions::exposureSpacing)
        .def_rw("anchor", &PavementOptions::anchor)
        .def_rw("fade", &PavementOptions::fade);
    m.def("validate_pavement_options", &ValidatePavementOptions, "options"_a, kValidateDoc);
    m.def("mars_desiccation_ridges", &MarsDesiccationRidges,
          "The desiccation ridges of Gale crater's Old Soaker slab (Stein et al. 2018).");
    auto layer = nb::class_<PavementLayer, Layer>(m, "PavementLayer", "Fractured bedrock plates.");
    layer.def(nb::init<PavementOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("relief_at", &PavementLayer::ReliefAt, "x"_a, "y"_a,
             "The pavement's relief at (x, y) in metres, before exposure and fading.")
        .def("edge_distance_at", &PavementLayer::EdgeDistanceAt, "x"_a, "y"_a,
             "The distance from (x, y) to the nearest plate edge, in metres.")
        .def_prop_ro("options", &PavementLayer::Options);
    BindMaterialCover<PavementLayer>(layer);
}

void BindStrata(nb::module_& m) {
    auto options = nb::class_<StrataOptions>(
        m, "StrataOptions",
        "Layered rock: beds `bed_thickness` metres thick, dipping `dip` toward `dip_azimuth`, each "
        "cut into a flat bench and a cliff by `sharpness`; angles in radians.");
    BindKwargsInit<StrataOptions>(options);
    options.def_rw("bed_thickness", &StrataOptions::bedThickness)
        .def_rw("thickness_variation", &StrataOptions::thicknessVariation)
        .def_rw("dip", &StrataOptions::dip)
        .def_rw("dip_azimuth", &StrataOptions::dipAzimuth)
        .def_rw("sharpness", &StrataOptions::sharpness)
        .def_rw("strength", &StrataOptions::strength)
        .def_rw("cliff_share", &StrataOptions::cliffShare)
        .def_rw("seed", &StrataOptions::seed)
        .def_rw("anchor", &StrataOptions::anchor);
    m.def("validate_strata_options", &ValidateStrataOptions, "options"_a, kValidateDoc);
    auto layer = nb::class_<StrataLayer, Layer>(m, "StrataLayer",
                                                "Layered rock terraced into benches and cliffs.");
    layer.def(nb::init<StrataOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("terraced_at", &StrataLayer::TerracedAt, "height"_a, "x"_a, "y"_a,
             "The terraced height of ground `height` metres high at (x, y).")
        .def("bed_position_at", &StrataLayer::BedPositionAt, "height"_a, "x"_a, "y"_a,
             "How far through its bed ground `height` metres high at (x, y) lies, in [0, 1].")
        .def_prop_ro("options", &StrataLayer::Options);
    BindMaterialCover<StrataLayer>(layer);
}

void BindDuneOptions(nb::module_& m) {
    nb::enum_<DuneShape>(m, "DuneShape", "The kind of dune a DuneLayer builds.")
        .value("BARCHAN", DuneShape::Barchan)
        .value("DOME", DuneShape::Dome)
        .value("TRANSVERSE", DuneShape::Transverse)
        .value("BARCHANOID", DuneShape::Barchanoid)
        .value("LINEAR", DuneShape::Linear);
    auto ripples = nb::class_<DuneRipples>(
        m, "DuneRipples",
        "Ripples on dunes: `wavelength` and `height` (m) on flat sand, turned and tightened by the "
        "dune's slope by `slope_coupling`; a wavelength of 0 gives none.");
    BindKwargsInit<DuneRipples>(ripples);
    ripples.def_rw("wavelength", &DuneRipples::wavelength)
        .def_rw("height", &DuneRipples::height)
        .def_rw("lee_fraction", &DuneRipples::leeFraction)
        .def_rw("sinuosity", &DuneRipples::sinuosity)
        .def_rw("slope_coupling", &DuneRipples::slopeCoupling)
        .def_rw("interdune_cover", &DuneRipples::interduneCover)
        .def_rw("forks", &DuneRipples::forks)
        .def_rw("fade", &DuneRipples::fade);
    auto options = nb::class_<DuneOptions>(
        m, "DuneOptions",
        "Dunes: barchans and domes of `width` (m) in cells `spacing` metres apart, or crest-line "
        "dunes `wavelength` metres apart and `height` metres high; angles in radians, the wind "
        "counter-clockwise from +x.");
    BindKwargsInit<DuneOptions>(options);
    options.def_rw("shape", &DuneOptions::shape)
        .def_rw("wind_azimuth", &DuneOptions::windAzimuth)
        .def_rw("azimuth_spread", &DuneOptions::azimuthSpread)
        .def_rw("slip_face_angle", &DuneOptions::slipFaceAngle)
        .def_rw("stoss_slope", &DuneOptions::stossSlope)
        .def_rw("stoss_exponent", &DuneOptions::stossExponent)
        .def_rw("dome_exponent", &DuneOptions::domeExponent)
        .def_rw("spacing", &DuneOptions::spacing)
        .def_rw("density", &DuneOptions::density)
        .def_rw("width", &DuneOptions::width)
        .def_rw("width_spread", &DuneOptions::widthSpread)
        .def_rw("min_width", &DuneOptions::minWidth)
        .def_rw("height_ratio", &DuneOptions::heightRatio)
        .def_rw("horn_length", &DuneOptions::hornLength)
        .def_rw("dome_length", &DuneOptions::domeLength)
        .def_rw("wavelength", &DuneOptions::wavelength)
        .def_rw("height", &DuneOptions::height)
        .def_rw("sinuosity", &DuneOptions::sinuosity)
        .def_rw("defects", &DuneOptions::defects)
        .def_rw("flank_slope", &DuneOptions::flankSlope)
        .def_rw("seed", &DuneOptions::seed)
        .def_rw("anchor", &DuneOptions::anchor)
        .def_rw("fade", &DuneOptions::fade)
        .def_rw("ripples", &DuneOptions::ripples);
    m.def("validate_dune_options", &ValidateDuneOptions, "options"_a, kValidateDoc);
    m.def("mars_dune_ripples", &MarsDuneRipples, "Martian large wind ripples to lay on dunes.");
    m.def("mars_barchans", &MarsBarchans, "Martian barchans with large ripples.");
    m.def("mars_transverse_dunes", &MarsTransverseDunes,
          "The transverse dunes of Mars's north polar erg, with large ripples.");
}

void BindDunes(nb::module_& m) {
    BindDuneOptions(m);
    auto dunes =
        nb::class_<DuneLayer, Layer>(m, "DuneLayer", "Wind-blown dunes, with ripples on them.");
    dunes.def(nb::init<DuneOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def("dune_height_at", &DuneLayer::DuneHeightAt, "x"_a, "y"_a,
             "The dunes' height at (x, y) in metres, without ripples.")
        .def_prop_ro("options", &DuneLayer::Options);
    BindMaterialCover<DuneLayer>(dunes);
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
    BindDunes(m);
    BindMaterialClasses(m);
    BindSandFill(m);
    BindPavement(m);
    BindStrata(m);
}

}  // namespace terraingen::python
