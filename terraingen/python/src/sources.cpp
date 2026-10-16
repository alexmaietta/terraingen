/// @file
/// @brief Bindings of the sources module: where heights come from, the base class Python can
/// extend, and synthetic terrain from layered noise.
#include "bindings.h"
#include "common.h"
#include "trampolines.h"

#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_terrain.h"

#include <nanobind/operators.h>

#include <optional>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief A source written in Python: `grid()` and `height_at(x, y)` are required; `fill(heights)`
/// fills a whole raster at once, and `reference_point()` gives (x, y) noise is measured from.
class PySource : public Source {
  public:
    NB_TRAMPOLINE(Source);

    RasterGrid Grid() const override {
        NB_OVERRIDE_PURE_NAME("grid", Grid);
    }

    Vec2d ReferencePoint() const override {
        return CallOverride<Vec2d>(nb_trampoline, "reference_point").value_or(Vec2d{});
    }

    void Fill(Raster<float>& heights) const override {
        if (CallOverrideIgnoringResult(nb_trampoline, "fill",
                                       ByReference<Raster<float>>{heights})) {
            return;
        }
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const Vec2d centre = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(centre.x, centre.y);
            }
        }
    }

    float HeightAt(double x, double y) const override {
        NB_OVERRIDE_PURE_NAME("height_at", HeightAt, x, y);
    }
};

void BindSourceBase(nb::module_& m) {
    nb::class_<Source, PySource>(
        m, "Source",
        "Where a terrain's heights come from, over its grid. Subclass it and implement `grid()` "
        "(a RasterGrid) and `height_at(x, y)`, and optionally `fill(heights)` and "
        "`reference_point()`, for a source of your own.")
        .def(nb::init<>())
        .def("grid", &Source::Grid)
        .def("height_at", &Source::HeightAt, "x"_a, "y"_a)
        .def("reference_point", &Source::ReferencePoint)
        .def("fill", &Source::Fill, "heights"_a, "Sets every cell of `heights`.")
        .def_prop_ro("width", [](const Source& source) { return source.Grid().width; })
        .def_prop_ro("height", [](const Source& source) { return source.Grid().height; })
        .def_prop_ro("transform", [](const Source& source) { return source.Grid().transform; });
}

void BindSourceTypes(nb::module_& m) {
    nb::class_<FlatSource, Source>(m, "FlatSource",
                                   "A level datum at `elevation` over a `width` x `height` grid of "
                                   "`pixel_size` metres centred on `centre`.")
        .def(
            "__init__",
            [](FlatSource* self, std::size_t width, std::size_t height, double pixelSize,
               float elevation, const Vec2d& centre, const Surface& surface) {
                new (self)
                    FlatSource(width, height, pixelSize / surface.UnitLength(), elevation, centre);
            },
            "width"_a, "height"_a, "pixel_size"_a, "elevation"_a = 0.0f, "centre"_a = Vec2d{},
            "surface"_a = Surface{},
            "On a sphere `centre` is (longitude, latitude) in degrees and `pixel_size` stays in "
            "metres, converted to degrees along a meridian.");
    nb::class_<GeoTiffSource, Source>(
        m, "GeoTiffSource",
        "Heights from band `band` of a GeoTIFF, optionally resampled to `pixel_size` metres and "
        "scaled (height * scale + offset).")
        .def(nb::init<std::filesystem::path, std::size_t, std::optional<double>, ResampleMethod,
                      std::optional<double>, std::optional<double>>(),
             "path"_a, "band"_a = 0, "pixel_size"_a = nb::none(),
             "resampling"_a = ResampleMethod::Cubic, "scale"_a = nb::none(),
             "offset"_a = nb::none(),
             "`pixel_size` is in the file's units (degrees for a geographic file).");
    nb::class_<SourceChain, Source>(
        m, "SourceChain",
        "Several sources tried in order, each filling where those before it have no data; the "
        "grid is the first's.")
        .def(
            "__init__",
            [](SourceChain* self, const std::vector<std::shared_ptr<Source>>& sources) {
                new (self) SourceChain({sources.begin(), sources.end()});
            },
            "sources"_a);
}

void BindSyntheticTerrain(nb::module_& m) {
    nb::class_<SyntheticTerrainParams>(m, "SyntheticTerrainParams",
                                       "A stack of 1 to 8 noise layers. `layers` is copied on "
                                       "access; assign a new list to change it.")
        .def(
            "__init__",
            [](SyntheticTerrainParams* params, std::optional<std::vector<NoiseLayer>> layers) {
                new (params) SyntheticTerrainParams{};
                if (layers) {
                    params->layers = *layers;
                }
            },
            "layers"_a = nb::none())
        .def_rw("layers", &SyntheticTerrainParams::layers)
        .def(nb::self == nb::self);
    m.def("validate_synthetic_terrain", &ValidateSyntheticTerrain, "params"_a,
          "Raise ValueError if the terrain parameters are unusable.");
    m.def("fill_synthetic_terrain", &FillSyntheticTerrain, "raster"_a, "params"_a,
          nb::call_guard<nb::gil_scoped_release>(),
          "Set every cell of `raster` to the synthetic terrain height at its centre.");
    m.def("generate_synthetic_terrain", &GenerateSyntheticTerrain, "width"_a, "height"_a,
          "pixel_size"_a, "params"_a, nb::call_guard<nb::gil_scoped_release>(),
          "Generate a north-up synthetic terrain centred on the world origin, with square pixels "
          "of `pixel_size` metres.");
}

}  // namespace

void BindSources(nb::module_& m) {
    BindSourceBase(m);
    BindSourceTypes(m);
    BindSyntheticTerrain(m);
}

}  // namespace terraingen::python
