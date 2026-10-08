#include "bindings.h"
#include "options.h"

#include "terraingen/terraingen.h"

#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/shared_ptr.h>
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

/// An albedo as Python builds it, with mutable parts as for `Terrain`.
struct PyAlbedo {
    std::shared_ptr<AlbedoSource> source;
    std::vector<std::shared_ptr<AlbedoLayer>> layers;

    Albedo Get() const {
        Albedo albedo;
        albedo.source = source;
        albedo.layers.assign(layers.begin(), layers.end());
        return albedo;
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
               std::vector<NoiseLayer> saturationNoise) {
                SyntheticAlbedoParams params;
                params.value = value;
                params.valueNoise = std::move(valueNoise);
                params.rgb = rgb;
                params.hue = hue;
                params.saturation = saturation;
                params.hueNoise = std::move(hueNoise);
                params.saturationNoise = std::move(saturationNoise);
                new (self) SyntheticAlbedo(std::move(params));
            },
            "value"_a = SyntheticAlbedoParams{}.value, "value_noise"_a = std::vector<NoiseLayer>{},
            "rgb"_a = false, "hue"_a = 0.0, "saturation"_a = 0.0,
            "hue_noise"_a = std::vector<NoiseLayer>{},
            "saturation_noise"_a = std::vector<NoiseLayer>{});

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
    nb::class_<CraterAlbedo, AlbedoLayer>(m, "CraterAlbedo",
                                          "Bright fresh craters and rays from the craters of "
                                          "`craters`, fading with age.")
        .def(
            "__init__",
            [](CraterAlbedo* self, std::shared_ptr<CraterLayer> craters, std::uint32_t seed,
               bool rays) {
                CraterAlbedoOptions options;
                options.seed = seed;
                options.rays.enabled = rays;
                new (self) CraterAlbedo(std::move(craters), options);
            },
            "craters"_a, "seed"_a = 0, "rays"_a = true);

    nb::class_<PyAlbedo>(m, "Albedo", "An albedo: `source`, changed by each of `layers` in order.")
        .def(
            "__init__",
            [](PyAlbedo* self, std::shared_ptr<AlbedoSource> source,
               std::vector<std::shared_ptr<AlbedoLayer>> layers) {
                new (self) PyAlbedo{std::move(source), std::move(layers)};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<AlbedoLayer>>{})
        .def_rw("source", &PyAlbedo::source)
        .def_rw("layers", &PyAlbedo::layers);

    m.def(
        "evaluate_albedo",
        [](const PyAlbedo& albedo, const RasterGrid& grid, double spacing, unsigned threads) {
            return EvaluateAlbedo(albedo.Get(), grid, AlbedoContext{spacing, threads});
        },
        "albedo"_a, "grid"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The albedo's bands over `grid` (such as `RasterGrid.of(heights)` or a source's "
        "`grid()`), sampled every `spacing` metres (0 for the grid's), one Raster per band.");

    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const std::vector<const Raster<float>*>& bands) {
            WriteGeoTiff(path, bands);
        },
        "path"_a, "bands"_a, "Writes one or more rasters as the bands of a float32 GeoTIFF.");
    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const Raster<float>& raster) {
            WriteGeoTiff(path, raster);
        },
        "path"_a, "raster"_a);
    m.def(
        "read_geotiff",
        [](const std::filesystem::path& path, std::size_t band) {
            return ReadGeoTiff(path, band).raster;
        },
        "path"_a, "band"_a = 0,
        "Reads band `band` (from 0) of a GeoTIFF as a Raster with the file's transform and no-data "
        "value.");
}

}  // namespace terraingen::python
