#include "bindings.h"

#include "terraingen/build_info.h"
#include "terraingen/core/geo_transform.h"
#include "terraingen/core/raster.h"
#include "terraingen/core/raster_stats.h"
#include "terraingen/version.h"

#include <nanobind/ndarray.h>
#include <nanobind/operators.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include <cstddef>
#include <cstdio>
#include <string>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

using RasterF = Raster<float>;
using NumpyView = nb::ndarray<nb::numpy, float, nb::ndim<2>, nb::c_contig>;
using ArrayApiView = nb::ndarray<nb::array_api, float, nb::ndim<2>, nb::c_contig>;

template <typename View> View MakeView(nb::handle self) {
    RasterF& raster = nb::cast<RasterF&>(self);
    return View(raster.Values().data(), {raster.Height(), raster.Width()}, self);
}

nb::tuple ToTuple(Vec2d v) {
    return nb::make_tuple(v.x, v.y);
}

std::string Repr(const GeoTransform& t) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "GeoTransform(%.17g, %.17g, %.17g, %.17g, %.17g, %.17g)",
                  t.originX, t.pixelWidth, t.rowRotation, t.originY, t.colRotation, t.pixelHeight);
    return buffer;
}

void BindVersion(nb::module_& m) {
    m.attr("__version__") = TERRAINGEN_VERSION_STRING;
    m.def(
        "version",
        [] {
            const Version v = GetVersion();
            return nb::make_tuple(v.major, v.minor, v.patch);
        },
        "Library version as (major, minor, patch).");
    m.def("api_version", &GetApiVersion, "Library API version.");
    m.def(
        "build_info",
        [] {
            const BuildInfo info = GetBuildInfo();
            nb::dict d;
            d["git_sha"] = info.gitSha;
            d["git_dirty"] = info.gitDirty;
            d["build_date"] = info.buildDate;
            return d;
        },
        "Build metadata of the compiled library: git_sha, git_dirty and build_date.");
}

void BindGeoTransform(nb::module_& m) {
    nb::class_<GeoTransform>(m, "GeoTransform",
                             "Affine pixel-to-world mapping matching GDAL's six-coefficient "
                             "geotransform. Pixel (0, 0) is the outer corner of the first pixel.")
        .def(nb::init<>())
        .def_rw("origin_x", &GeoTransform::originX)
        .def_rw("pixel_width", &GeoTransform::pixelWidth)
        .def_rw("row_rotation", &GeoTransform::rowRotation)
        .def_rw("origin_y", &GeoTransform::originY)
        .def_rw("col_rotation", &GeoTransform::colRotation)
        .def_rw("pixel_height", &GeoTransform::pixelHeight)
        .def_static("north_up", &GeoTransform::NorthUp, "origin_x"_a, "origin_y"_a, "pixel_size"_a,
                    "North-up transform with square pixels.")
        .def_static("from_gdal", &GeoTransform::FromGdal, "coefficients"_a,
                    "Build from GDAL's coefficient order.")
        .def(
            "to_gdal",
            [](const GeoTransform& t) {
                const auto c = t.ToGdal();
                return nb::make_tuple(c[0], c[1], c[2], c[3], c[4], c[5]);
            },
            "Coefficients in GDAL's order.")
        .def(
            "pixel_to_world",
            [](const GeoTransform& t, double col, double row) {
                return ToTuple(t.PixelToWorld(col, row));
            },
            "col"_a, "row"_a, "Map pixel coordinates to world coordinates (x, y).")
        .def(
            "world_to_pixel",
            [](const GeoTransform& t, double x, double y) { return ToTuple(t.WorldToPixel(x, y)); },
            "x"_a, "y"_a,
            "Map world coordinates to pixel coordinates (col, row). Raises ValueError if the "
            "transform is not invertible.")
        .def(nb::self == nb::self)
        .def("__repr__", &Repr);
}

void BindRaster(nb::module_& m) {
    nb::class_<RasterF>(m, "Raster",
                        "Single-band float32 raster, stored row-major with row 0 at the top. "
                        "Shares memory with NumPy and any DLPack consumer without copying.")
        .def(nb::init<std::size_t, std::size_t, float>(), "width"_a, "height"_a, "fill"_a = 0.0f)
        .def_static(
            "from_array",
            [](nb::ndarray<const float, nb::ndim<2>, nb::device::cpu> array,
               const GeoTransform& transform) {
                RasterF raster(array.shape(1), array.shape(0));
                const auto view = array.view();
                for (std::size_t row = 0; row < raster.Height(); ++row) {
                    for (std::size_t col = 0; col < raster.Width(); ++col) {
                        raster(col, row) = view(row, col);
                    }
                }
                raster.SetTransform(transform);
                return raster;
            },
            "array"_a, "transform"_a = GeoTransform{},
            "Copy a 2D array of shape (height, width) from NumPy, DLPack or the buffer protocol.")
        .def_prop_ro("width", &RasterF::Width)
        .def_prop_ro("height", &RasterF::Height)
        .def_prop_ro("shape",
                     [](const RasterF& r) { return nb::make_tuple(r.Height(), r.Width()); })
        .def_prop_ro("values", &MakeView<NumpyView>,
                     "Writeable NumPy view of shape (height, width) that keeps the raster alive.")
        .def_prop_rw("transform", &RasterF::Transform, &RasterF::SetTransform)
        .def_prop_rw("crs", &RasterF::Crs, &RasterF::SetCrs, "Coordinate reference system as WKT.")
        .def_prop_rw("nodata", &RasterF::NoData, &RasterF::SetNoData)
        .def(
            "cell_center",
            [](const RasterF& r, std::size_t col, std::size_t row) {
                return ToTuple(r.CellCenter(col, row));
            },
            "col"_a, "row"_a, "World coordinates of a cell's centre.")
        .def(
            "__array__",
            [](nb::handle self, nb::object dtype, nb::object copy) {
                nb::object values = nb::cast(MakeView<NumpyView>(self));
                if (!dtype.is_none()) {
                    values = values.attr("astype")(dtype);
                }
                if (!copy.is_none() && nb::cast<bool>(copy)) {
                    values = values.attr("copy")();
                }
                return values;
            },
            "dtype"_a = nb::none(), "copy"_a = nb::none())
        .def(
            "__dlpack__",
            [](nb::handle self, nb::kwargs kwargs) {
                return nb::cast(MakeView<ArrayApiView>(self)).attr("__dlpack__")(**kwargs);
            },
            "kwargs"_a)
        .def("__dlpack_device__",
             [](nb::handle self) {
                 return nb::cast(MakeView<ArrayApiView>(self)).attr("__dlpack_device__")();
             })
        .def("__repr__", [](const RasterF& r) {
            return "Raster(width=" + std::to_string(r.Width()) +
                   ", height=" + std::to_string(r.Height()) + ")";
        });
}

void BindStats(nb::module_& m) {
    nb::class_<RasterStats>(m, "RasterStats", "Statistics of a raster's valid cells.")
        .def_ro("min", &RasterStats::min)
        .def_ro("max", &RasterStats::max)
        .def_ro("mean", &RasterStats::mean)
        .def_ro("valid_count", &RasterStats::validCount)
        .def("__repr__", [](const RasterStats& s) {
            return "RasterStats(min=" + std::to_string(s.min) + ", max=" + std::to_string(s.max) +
                   ", mean=" + std::to_string(s.mean) +
                   ", valid_count=" + std::to_string(s.validCount) + ")";
        });
    m.def("compute_stats", &ComputeStats<float>, "raster"_a,
          "Statistics over cells that are finite and not the nodata value.");
}

}  // namespace

void BindCore(nb::module_& m) {
    BindVersion(m);
    BindGeoTransform(m);
    BindRaster(m);
    BindStats(m);
}

}  // namespace terraingen::python
