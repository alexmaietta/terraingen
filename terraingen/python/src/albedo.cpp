#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "trampolines.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"

#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief One albedo value from Python: a number for one band, or a sequence of up to three.
AlbedoSample ToSample(nb::handle value) {
    AlbedoSample sample{};
    if (nb::isinstance<nb::float_>(value) || nb::isinstance<nb::int_>(value)) {
        sample.fill(nb::cast<float>(value));
        return sample;
    }
    std::size_t band = 0;
    for (nb::handle item : value) {
        if (band == sample.size()) {
            throw std::invalid_argument("An albedo value has at most three bands");
        }
        sample[band++] = nb::cast<float>(item);
    }
    return sample;
}

/// @brief The first `bands` values of `sample`.
std::vector<float> Bands(const AlbedoSample& sample, std::size_t bands) {
    return std::vector<float>(sample.begin(), sample.begin() + static_cast<long>(bands));
}

/// @brief An albedo source written in Python: `bands()` (1 or 3) and `value_at(x, y, context)`, a
/// number or a sequence of `bands()` numbers.
class PyAlbedoSource : public AlbedoSource {
  public:
    NB_TRAMPOLINE(AlbedoSource);

    std::size_t Bands() const override {
        NB_OVERRIDE_PURE_NAME("bands", Bands);
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override {
        return CallRequired(nb_trampoline, "value_at", &ToSample, x, y, context);
    }
};

/// @brief An albedo layer written in Python: `apply_at(value, bands, x, y, context)` returns the
/// new value (a number or a sequence of `bands` numbers) from the one so far (a list).
class PyAlbedoLayer : public AlbedoLayer {
  public:
    NB_TRAMPOLINE(AlbedoLayer);

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override {
        value = CallRequired(nb_trampoline, "apply_at", &ToSample, Bands(value, bands), bands, x, y,
                             context);
    }
};

void BindBases(nb::module_& m) {
    nb::class_<AlbedoContext>(m, "AlbedoContext", "How an albedo is evaluated.")
        .def(
            "__init__",
            [](AlbedoContext* self, double spacing, unsigned threads) {
                new (self) AlbedoContext{spacing, threads};
            },
            "spacing"_a = 1.0, "threads"_a = 1)
        .def_ro("spacing", &AlbedoContext::spacing)
        .def_ro("threads", &AlbedoContext::threads);
    m.def("validate_albedo_context", &ValidateAlbedoContext, "context"_a, kValidateDoc);
    nb::class_<AlbedoSource, PyAlbedoSource>(
        m, "AlbedoSource",
        "Where an albedo starts. Subclass it and implement `bands()` (1 or 3) and `value_at(x, "
        "y, context)` for a source of your own.")
        .def(nb::init<>())
        .def("bands", &AlbedoSource::Bands)
        .def("value_at", &AlbedoSource::ValueAt, "x"_a, "y"_a, "context"_a);
    nb::class_<AlbedoLayer, PyAlbedoLayer>(
        m, "AlbedoLayer",
        "A change to an albedo. Subclass it and implement `apply_at(value, bands, x, y, "
        "context)` for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply_at",
            [](const AlbedoLayer& layer, const std::vector<float>& value, std::size_t bands,
               double x, double y, const AlbedoContext& context) {
                AlbedoSample sample{};
                for (std::size_t band = 0; band < value.size() && band < sample.size(); ++band) {
                    sample[band] = value[band];
                }
                layer.ApplyAt(sample, bands, x, y, context);
                return Bands(sample, bands);
            },
            "value"_a, "bands"_a, "x"_a, "y"_a, "context"_a);
}

void BindSynthetic(nb::module_& m) {
    nb::class_<SyntheticAlbedo, AlbedoSource>(
        m, "SyntheticAlbedo",
        "A synthetic albedo: `value` (reflectance), varied by `value_noise` (relative), and with "
        "`rgb` three bands from `hue` and `saturation` and their noise.")
        .def(
            "__init__",
            [](SyntheticAlbedo* self, double value, std::vector<NoiseLayer> valueNoise, bool rgb,
               double hue, double saturation, std::vector<NoiseLayer> hueNoise,
               std::vector<NoiseLayer> saturationNoise, const Vec2d& origin,
               const Surface& surface) {
                SyntheticAlbedoParams params;
                params.value = value;
                params.valueNoise = std::move(valueNoise);
                params.rgb = rgb;
                params.hue = hue;
                params.saturation = saturation;
                params.hueNoise = std::move(hueNoise);
                params.saturationNoise = std::move(saturationNoise);
                new (self) SyntheticAlbedo(std::move(params), origin, surface);
            },
            "value"_a = SyntheticAlbedoParams{}.value, "value_noise"_a = std::vector<NoiseLayer>{},
            "rgb"_a = false, "hue"_a = 0.0, "saturation"_a = 0.0,
            "hue_noise"_a = std::vector<NoiseLayer>{},
            "saturation_noise"_a = std::vector<NoiseLayer>{}, "origin"_a = Vec2d{},
            "surface"_a = Surface{});
    m.def(
        "hsv_to_rgb",
        [](double hue, double saturation, double value) {
            const AlbedoSample rgb = HsvToRgb(hue, saturation, value);
            return std::tuple{rgb[0], rgb[1], rgb[2]};
        },
        "hue"_a, "saturation"_a, "value"_a, "A colour's (red, green, blue), hue in degrees.");
}

void BindRasterAlbedo(nb::module_& m) {
    auto detail = nb::class_<AlbedoDetail>(
        m, "AlbedoDetail",
        "Sub-cell detail added to an albedo map: `octaves` of noise carrying the map's measured "
        "roughness (or `hurst`) one octave down at a time, times `scale`.");
    BindKwargsInit<AlbedoDetail>(detail);
    detail.def_rw("enabled", &AlbedoDetail::enabled)
        .def_rw("seed", &AlbedoDetail::seed)
        .def_rw("octaves", &AlbedoDetail::octaves)
        .def_rw("scale", &AlbedoDetail::scale)
        .def_rw("hurst", &AlbedoDetail::hurst)
        .def_rw("fade", &AlbedoDetail::fade);
    m.def("validate_albedo_detail", &ValidateAlbedoDetail, "detail"_a, kValidateDoc);
    auto options = nb::class_<RasterAlbedoOptions>(
        m, "RasterAlbedoOptions", "How an albedo map is interpolated and given detail.");
    BindKwargsInit<RasterAlbedoOptions>(options);
    options.def_rw("method", &RasterAlbedoOptions::method)
        .def_rw("detail", &RasterAlbedoOptions::detail);
    nb::class_<RasterAlbedo, AlbedoSource>(
        m, "RasterAlbedo",
        "An albedo map (1 or 3 bands, such as a normal albedo mosaic from orbit) interpolated to "
        "the grid and given sub-cell detail; NaN outside it.")
        .def(nb::init<std::vector<Raster<float>>, RasterAlbedoOptions, Surface>(), "bands"_a,
             "options"_a = RasterAlbedoOptions{}, "surface"_a = Surface{})
        .def_prop_ro("hurst", &RasterAlbedo::Hurst, "The Hurst exponent the detail uses.")
        .def_prop_ro("cell_roughness", &RasterAlbedo::CellRoughness,
                     "The roughness of the log brightness at one cell, as measured.");
}

void BindRetention(nb::module_& m) {
    auto retention = nb::class_<RayRetention>(
        m, "RayRetention",
        "How long rays stay visible (Myr) by crater diameter (m): `small_myr` up to "
        "`small_diameter`, `mid_myr` from `mid_diameter`, `large_myr` above `large_diameter`, "
        "all times `scale`.");
    BindKwargsInit<RayRetention>(retention);
    retention.def_rw("small_diameter", &RayRetention::smallDiameter)
        .def_rw("small_myr", &RayRetention::smallMyr)
        .def_rw("mid_diameter", &RayRetention::midDiameter)
        .def_rw("mid_myr", &RayRetention::midMyr)
        .def_rw("large_diameter", &RayRetention::largeDiameter)
        .def_rw("large_myr", &RayRetention::largeMyr)
        .def_rw("scale", &RayRetention::scale)
        .def("myr", &RayRetention::Myr, "diameter"_a);
    m.def("validate_ray_retention", &ValidateRayRetention, "retention"_a, kValidateDoc);
}

void BindRays(nb::module_& m) {
    auto oblique = nb::class_<ObliqueImpacts>(
        m, "ObliqueImpacts",
        "Ray patterns of oblique impacts: below `asymmetric_degrees` the ejecta is asymmetric, "
        "below `forbidden_degrees` an uprange zone has no rays, below `butterfly_degrees` rays "
        "form a butterfly; impacts come from `heading` (degrees from north) or any direction.");
    BindKwargsInit<ObliqueImpacts>(oblique);
    oblique.def_rw("enabled", &ObliqueImpacts::enabled)
        .def_rw("heading", &ObliqueImpacts::heading)
        .def_rw("asymmetric_degrees", &ObliqueImpacts::asymmetricDegrees)
        .def_rw("forbidden_degrees", &ObliqueImpacts::forbiddenDegrees)
        .def_rw("butterfly_degrees", &ObliqueImpacts::butterflyDegrees)
        .def_rw("downrange_speed_ratio", &ObliqueImpacts::downrangeSpeedRatio)
        .def_rw("zone_half_width_degrees", &ObliqueImpacts::zoneHalfWidthDegrees);
    m.def("validate_oblique_impacts", &ValidateObliqueImpacts, "oblique"_a, kValidateDoc);
    auto rays = nb::class_<CraterRays>(
        m, "CraterRays",
        "Bright rays: how many (`count`), how long (Elliott et al. 2018: `length_coefficient` * "
        "(D/2 in km)^`length_exponent` km, spread by `length_spread`), how wide, how bright "
        "(`excess` maturity) and how patchy.");
    BindKwargsInit<CraterRays>(rays);
    rays.def_rw("enabled", &CraterRays::enabled)
        .def_rw("count", &CraterRays::count)
        .def_rw("length_coefficient", &CraterRays::lengthCoefficient)
        .def_rw("length_exponent", &CraterRays::lengthExponent)
        .def_rw("length_spread", &CraterRays::lengthSpread)
        .def_rw("width_degrees", &CraterRays::widthDegrees)
        .def_rw("excess", &CraterRays::excess)
        .def_rw("patchiness", &CraterRays::patchiness)
        .def_rw("patch_radii", &CraterRays::patchRadii)
        .def_rw("oblique", &CraterRays::oblique)
        .def("median_length", &CraterRays::MedianLength, "diameter"_a);
    m.def("validate_crater_rays", &ValidateCraterRays, "rays"_a, kValidateDoc);
}

void BindCraterAlbedoOptions(nb::module_& m) {
    auto texture = nb::class_<CraterAlbedoTexture>(
        m, "CraterAlbedoTexture",
        "Texture of fresh craters' bright material: `streaks` breaking it into filaments "
        "`streak_width_degrees` wide and `streak_length_radii` long, `lobes` (`lobe_count` of "
        "them) wandering the ejecta edge, and `floor_share` dimming floors under walls.");
    BindKwargsInit<CraterAlbedoTexture>(texture);
    texture.def_rw("streaks", &CraterAlbedoTexture::streaks)
        .def_rw("streak_width_degrees", &CraterAlbedoTexture::streakWidthDegrees)
        .def_rw("streak_length_radii", &CraterAlbedoTexture::streakLengthRadii)
        .def_rw("continuous_radii", &CraterAlbedoTexture::continuousRadii)
        .def_rw("lobes", &CraterAlbedoTexture::lobes)
        .def_rw("lobe_count", &CraterAlbedoTexture::lobeCount)
        .def_rw("floor_share", &CraterAlbedoTexture::floorShare)
        .def_rw("seed", &CraterAlbedoTexture::seed);
    m.def("validate_crater_albedo_texture", &ValidateCraterAlbedoTexture, "texture"_a,
          kValidateDoc);
    auto options = nb::class_<CraterAlbedoOptions>(
        m, "CraterAlbedoOptions",
        "Which craters get bright material (diameters, ages, `share`), how much brighter their "
        "interiors and ejecta are in optical maturity, their rays, and how fast it all fades.");
    BindKwargsInit<CraterAlbedoOptions>(options);
    options.def_rw("seed", &CraterAlbedoOptions::seed)
        .def_rw("selection", &CraterAlbedoOptions::selection);
    BindSelection(options, &CraterAlbedoOptions::selection, "min_diameter", "max_diameter");
    options.def_rw("share", &CraterAlbedoOptions::share)
        .def_rw("background_maturity", &CraterAlbedoOptions::backgroundMaturity)
        .def_rw("max_maturity", &CraterAlbedoOptions::maxMaturity)
        .def_rw("interior_excess", &CraterAlbedoOptions::interiorExcess)
        .def_rw("ejecta_excess", &CraterAlbedoOptions::ejectaExcess)
        .def_rw("ejecta_exponent", &CraterAlbedoOptions::ejectaExponent)
        .def_rw("ejecta_reach_radii", &CraterAlbedoOptions::ejectaReachRadii)
        .def_rw("rays", &CraterAlbedoOptions::rays)
        .def_rw("texture", &CraterAlbedoOptions::texture)
        .def_rw("retention", &CraterAlbedoOptions::retention)
        .def_rw("fade_exponent", &CraterAlbedoOptions::fadeExponent)
        .def_rw("origin_reflectance", &CraterAlbedoOptions::originReflectance)
        .def_rw("fade", &CraterAlbedoOptions::fade);
    m.def("validate_crater_albedo", &ValidateCraterAlbedo, "options"_a, kValidateDoc);
}

void BindCraterAlbedo(nb::module_& m) {
    BindRetention(m);
    BindRays(m);
    BindCraterAlbedoOptions(m);
    nb::class_<CraterAlbedo, AlbedoLayer>(m, "CraterAlbedo",
                                          "Bright fresh craters and rays from the craters of "
                                          "`craters`, fading with age.")
        .def(nb::init<std::shared_ptr<CraterLayer>, CraterAlbedoOptions>(), "craters"_a,
             "options"_a = CraterAlbedoOptions{})
        .def("excess", &CraterAlbedo::Excess, "crater"_a, "x"_a, "y"_a, "spacing"_a,
             "The excess optical maturity `crater` gives (x, y).")
        .def("impact", &CraterAlbedo::Impact, "crater"_a,
             "The impact `crater` came from, drawn from its ID.")
        .def("reach", &CraterAlbedo::Reach, "crater"_a,
             "How far (m) from its centre `crater` can brighten the ground.");
    nb::class_<CraterImpact>(m, "CraterImpact",
                             "An impact: its angle from the horizontal, downrange heading and "
                             "downrange-to-uprange ejection speed ratio.")
        .def_ro("angle_degrees", &CraterImpact::angleDegrees)
        .def_ro("heading", &CraterImpact::heading, "Radians counter-clockwise from east.")
        .def_ro("speed_ratio", &CraterImpact::speedRatio);
}

void BindMaturity(nb::module_& m) {
    auto origin = nb::class_<OpticalMaturityOrigin>(
        m, "OpticalMaturityOrigin",
        "The origin of Lucey et al.'s optical maturity: the reflectance at 750 nm and the 950/750 "
        "ratio that fully immature material tends to.");
    BindKwargsInit<OpticalMaturityOrigin>(origin);
    origin.def_rw("reflectance", &OpticalMaturityOrigin::reflectance)
        .def_rw("ratio", &OpticalMaturityOrigin::ratio);
    m.def("optical_maturity", &OpticalMaturity, "r750"_a, "r950"_a,
          "origin"_a = OpticalMaturityOrigin{},
          "Lucey et al.'s (2000) optical maturity of reflectances at 750 and 950 nm.");
    m.def("reflectance_at_maturity", &ReflectanceAtMaturity, "reflectance"_a, "reference"_a,
          "maturity"_a, "origin_reflectance"_a = OpticalMaturityOrigin{}.reflectance,
          "The reflectance of material at `maturity`, from `reflectance` at the `reference` "
          "maturity.");
}

void BindEvaluation(nb::module_& m) {
    nb::class_<PyAlbedo>(m, "Albedo", "An albedo: `source`, changed by each of `layers` in order.")
        .def(
            "__init__",
            [](PyAlbedo* self, std::shared_ptr<AlbedoSource> source,
               std::vector<std::shared_ptr<AlbedoLayer>> layers, const Surface& surface) {
                new (self) PyAlbedo{std::move(source), std::move(layers), surface};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<AlbedoLayer>>{},
            "surface"_a = Surface{})
        .def_rw("surface", &PyAlbedo::surface)
        .def_rw("source", &PyAlbedo::source)
        .def_rw("layers", &PyAlbedo::layers);
    m.def(
        "evaluate_albedo_at",
        [](const PyAlbedo& albedo, double x, double y, double spacing) {
            const AlbedoSample sample =
                EvaluateAlbedoAt(albedo.Get(), x, y, AlbedoContext{spacing, 1});
            return Bands(sample, albedo.source ? albedo.source->Bands() : 1);
        },
        "albedo"_a, "x"_a, "y"_a, "spacing"_a,
        "The albedo's bands at (x, y) sampled every `spacing` metres, equal to the grid cell "
        "centred there.");
    m.def(
        "evaluate_albedo",
        [](const PyAlbedo& albedo, const RasterGrid& grid, double spacing, unsigned threads) {
            return EvaluateAlbedo(albedo.Get(), grid, AlbedoContext{spacing, threads});
        },
        "albedo"_a, "grid"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The albedo's bands over `grid` (such as `RasterGrid.of(heights)` or a source's "
        "`grid()`), sampled every `spacing` metres (0 for the grid's), one Raster per band.");
}

}  // namespace

void BindAlbedo(nb::module_& m) {
    BindBases(m);
    BindSynthetic(m);
    BindRasterAlbedo(m);
    BindCraterAlbedo(m);
    BindMaturity(m);
    BindEvaluation(m);
}

}  // namespace terraingen::python
