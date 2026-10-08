#include "bindings.h"
#include "objects.h"
#include "options.h"

#include "terraingen/terraingen.h"

#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

/// One albedo value from Python: a number for one band, or a sequence of up to three.
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

/// An albedo source written in Python: `bands()` (1 or 3) and `value_at(x, y, context)`, a
/// number or a sequence of `bands()` numbers.
class PyAlbedoSource : public AlbedoSource {
  public:
    NB_TRAMPOLINE(AlbedoSource);

    std::size_t Bands() const override {
        NB_OVERRIDE_PURE_NAME("bands", Bands);
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("value_at");
        const nb::detail::ticket ticket(nb_trampoline, "value_at", hash, true);
        return ToSample(nb_trampoline.base().attr(ticket.key)(x, y, context));
    }
};

/// An albedo layer written in Python: `apply_at(value, bands, x, y, context)` returns the new
/// value (a number or a sequence of `bands` numbers) from the one so far (a list).
class PyAlbedoLayer : public AlbedoLayer {
  public:
    NB_TRAMPOLINE(AlbedoLayer);

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("apply_at");
        const nb::detail::ticket ticket(nb_trampoline, "apply_at", hash, true);
        nb::list current;
        for (std::size_t band = 0; band < bands; ++band) {
            current.append(value[band]);
        }
        value = ToSample(nb_trampoline.base().attr(ticket.key)(current, bands, x, y, context));
    }
};

}  // namespace

void BindAlbedo(nb::module_& m) {
    nb::class_<AlbedoContext>(m, "AlbedoContext", "How an albedo is evaluated.")
        .def(
            "__init__",
            [](AlbedoContext* self, double spacing, unsigned threads) {
                new (self) AlbedoContext{spacing, threads};
            },
            "spacing"_a = 1.0, "threads"_a = 1)
        .def_ro("spacing", &AlbedoContext::spacing)
        .def_ro("threads", &AlbedoContext::threads);

    nb::class_<AlbedoSource, PyAlbedoSource>(
        m, "AlbedoSource",
        "Where an albedo starts. Subclass it and implement `bands()` (1 or 3) and `value_at(x, "
        "y, context)` for a source of your own.")
        .def(nb::init<>())
        .def("bands", &AlbedoSource::Bands)
        .def("value_at", &AlbedoSource::ValueAt, "x"_a, "y"_a, "context"_a);
    nb::class_<SyntheticAlbedo, AlbedoSource>(
        m, "SyntheticAlbedo",
        "A synthetic albedo: `value` (reflectance), varied by `value_noise` (relative), and with "
        "`rgb` three bands from `hue` and `saturation` and their noise.")
        .def(
            "__init__",
            [](SyntheticAlbedo* self, double value, std::vector<NoiseLayer> valueNoise, bool rgb,
               double hue, double saturation, std::vector<NoiseLayer> hueNoise,
               std::vector<NoiseLayer> saturationNoise, std::pair<double, double> origin,
               const Surface& surface) {
                SyntheticAlbedoParams params;
                params.value = value;
                params.valueNoise = std::move(valueNoise);
                params.rgb = rgb;
                params.hue = hue;
                params.saturation = saturation;
                params.hueNoise = std::move(hueNoise);
                params.saturationNoise = std::move(saturationNoise);
                new (self)
                    SyntheticAlbedo(std::move(params), {origin.first, origin.second}, surface);
            },
            "value"_a = SyntheticAlbedoParams{}.value, "value_noise"_a = std::vector<NoiseLayer>{},
            "rgb"_a = false, "hue"_a = 0.0, "saturation"_a = 0.0,
            "hue_noise"_a = std::vector<NoiseLayer>{},
            "saturation_noise"_a = std::vector<NoiseLayer>{}, "origin"_a = std::pair{0.0, 0.0},
            "surface"_a = Surface{});

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
    auto rasterOptions = nb::class_<RasterAlbedoOptions>(
        m, "RasterAlbedoOptions", "How an albedo map is interpolated and given detail.");
    BindKwargsInit<RasterAlbedoOptions>(rasterOptions);
    rasterOptions.def_rw("method", &RasterAlbedoOptions::method)
        .def_rw("detail", &RasterAlbedoOptions::detail);
    nb::class_<RasterAlbedo, AlbedoSource>(
        m, "RasterAlbedo",
        "An albedo map (1 or 3 bands, such as a normal albedo mosaic from orbit) interpolated to "
        "the grid and given sub-cell detail; NaN outside it.")
        .def(
            "__init__",
            [](RasterAlbedo* self, std::vector<Raster<float>> bands, RasterAlbedoOptions options,
               const Surface& surface) {
                new (self) RasterAlbedo(std::move(bands), std::move(options), surface);
            },
            "bands"_a, "options"_a = RasterAlbedoOptions{}, "surface"_a = Surface{})
        .def_prop_ro("hurst", &RasterAlbedo::Hurst, "The Hurst exponent the detail uses.")
        .def_prop_ro("cell_roughness", &RasterAlbedo::CellRoughness,
                     "The roughness of the log brightness at one cell, as measured.");

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
                return std::vector<float>(sample.begin(), sample.begin() + bands);
            },
            "value"_a, "bands"_a, "x"_a, "y"_a, "context"_a);
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
    auto craterOptions = nb::class_<CraterAlbedoOptions>(
        m, "CraterAlbedoOptions",
        "Which craters get bright material (diameters, ages, `share`), how much brighter their "
        "interiors and ejecta are in optical maturity, their rays, and how fast it all fades.");
    BindKwargsInit<CraterAlbedoOptions>(craterOptions);
    craterOptions.def_rw("seed", &CraterAlbedoOptions::seed)
        .def_rw("min_diameter", &CraterAlbedoOptions::minDiameter)
        .def_rw("max_diameter", &CraterAlbedoOptions::maxDiameter)
        .def_rw("min_age_gyr", &CraterAlbedoOptions::minAgeGyr)
        .def_rw("max_age_gyr", &CraterAlbedoOptions::maxAgeGyr)
        .def_rw("manual", &CraterAlbedoOptions::manual)
        .def_rw("share", &CraterAlbedoOptions::share)
        .def_rw("background_maturity", &CraterAlbedoOptions::backgroundMaturity)
        .def_rw("max_maturity", &CraterAlbedoOptions::maxMaturity)
        .def_rw("interior_excess", &CraterAlbedoOptions::interiorExcess)
        .def_rw("ejecta_excess", &CraterAlbedoOptions::ejectaExcess)
        .def_rw("ejecta_exponent", &CraterAlbedoOptions::ejectaExponent)
        .def_rw("ejecta_reach_radii", &CraterAlbedoOptions::ejectaReachRadii)
        .def_rw("rays", &CraterAlbedoOptions::rays)
        .def_rw("retention", &CraterAlbedoOptions::retention)
        .def_rw("fade_exponent", &CraterAlbedoOptions::fadeExponent)
        .def_rw("origin_reflectance", &CraterAlbedoOptions::originReflectance)
        .def_rw("fade", &CraterAlbedoOptions::fade);
    nb::class_<CraterAlbedo, AlbedoLayer>(m, "CraterAlbedo",
                                          "Bright fresh craters and rays from the craters of "
                                          "`craters`, fading with age.")
        .def(
            "__init__",
            [](CraterAlbedo* self, std::shared_ptr<CraterLayer> craters,
               CraterAlbedoOptions options) {
                new (self) CraterAlbedo(std::move(craters), std::move(options));
            },
            "craters"_a, "options"_a = CraterAlbedoOptions{})
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
            const std::size_t bands = albedo.source ? albedo.source->Bands() : 1;
            return std::vector<float>(sample.begin(), sample.begin() + static_cast<long>(bands));
        },
        "albedo"_a, "x"_a, "y"_a, "spacing"_a,
        "The albedo's bands at (x, y) sampled every `spacing` metres, equal to the grid cell "
        "centred there.");
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
    m.def(
        "hsv_to_rgb",
        [](double hue, double saturation, double value) {
            const AlbedoSample rgb = HsvToRgb(hue, saturation, value);
            return std::tuple{rgb[0], rgb[1], rgb[2]};
        },
        "hue"_a, "saturation"_a, "value"_a, "A colour's (red, green, blue), hue in degrees.");

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

}  // namespace terraingen::python
