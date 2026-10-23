// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/raster_stats.h"
#include "terraingen/raster/resample.h"

#include <nanobind/ndarray.h>
#include <nanobind/operators.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace terraingen::python {

namespace {

using RasterF = Raster<float>;
using NumpyView = nb::ndarray<nb::numpy, float, nb::ndim<2>, nb::c_contig>;
using ArrayApiView = nb::ndarray<nb::array_api, float, nb::ndim<2>, nb::c_contig>;

/// @brief A view of the raster `self` that keeps it alive.
template <typename View> View MakeView(nb::handle self) {
    RasterF& raster = nb::cast<RasterF&>(self);
    return View(raster.Values().data(), {raster.Height(), raster.Width()}, self);
}

std::string Repr(const GeoTransform& t) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "GeoTransform(%.17g, %.17g, %.17g, %.17g, %.17g, %.17g)",
                  t.originX, t.pixelWidth, t.rowRotation, t.originY, t.colRotation, t.pixelHeight);
    return buffer;
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
        .def("pixel_to_world", &GeoTransform::PixelToWorld, "col"_a, "row"_a,
             "Map pixel coordinates to world coordinates (x, y).")
        .def("world_to_pixel", &GeoTransform::WorldToPixel, "x"_a, "y"_a,
             "Map world coordinates to pixel coordinates (col, row). Raises ValueError if the "
             "transform is not invertible.")
        .def(nb::self == nb::self)
        .def("__repr__", &Repr);
}

RasterF RasterFromArray(const CpuMatrix<float>& array, const GeoTransform& transform) {
    RasterF raster(array.shape(1), array.shape(0));
    std::copy(array.data(), array.data() + array.size(), raster.Values().begin());
    raster.SetTransform(transform);
    return raster;
}

nb::object RasterArray(nb::handle self, nb::object dtype, nb::object copy) {
    nb::object values = nb::cast(MakeView<NumpyView>(self));
    if (!dtype.is_none()) {
        values = values.attr("astype")(dtype);
    }
    if (!copy.is_none() && nb::cast<bool>(copy)) {
        values = values.attr("copy")();
    }
    return values;
}

void BindArrayProtocols(nb::class_<RasterF>& raster) {
    raster.def("__array__", &RasterArray, "dtype"_a = nb::none(), "copy"_a = nb::none())
        .def(
            "__dlpack__",
            [](nb::handle self, nb::kwargs kwargs) {
                return nb::cast(MakeView<ArrayApiView>(self)).attr("__dlpack__")(**kwargs);
            },
            "kwargs"_a)
        .def("__dlpack_device__", [](nb::handle self) {
            return nb::cast(MakeView<ArrayApiView>(self)).attr("__dlpack_device__")();
        });
}

void BindRasterClass(nb::module_& m) {
    nb::class_<RasterF> raster(m, "Raster",
                               "Single-band float32 raster, stored row-major with row 0 at the "
                               "top. Shares memory with NumPy and any DLPack consumer without "
                               "copying.");
    raster.def(nb::init<std::size_t, std::size_t, float>(), "width"_a, "height"_a, "fill"_a = 0.0f)
        .def_static("from_array", &RasterFromArray, "array"_a, "transform"_a = GeoTransform{},
                    "Copy a 2D array of shape (height, width) from NumPy, DLPack or the buffer "
                    "protocol.")
        .def_prop_ro("width", &RasterF::Width)
        .def_prop_ro("height", &RasterF::Height)
        .def_prop_ro("shape",
                     [](const RasterF& r) { return nb::make_tuple(r.Height(), r.Width()); })
        .def_prop_ro("values", &MakeView<NumpyView>,
                     "Writeable NumPy view of shape (height, width) that keeps the raster alive.")
        .def_prop_rw("transform", &RasterF::Transform, &RasterF::SetTransform)
        .def_prop_rw("no_data", &RasterF::NoData, &RasterF::SetNoData)
        .def("cell_center", &RasterF::CellCenter, "col"_a, "row"_a,
             "World coordinates of a cell's center.")
        .def("__repr__", [](const RasterF& r) {
            return "Raster(width=" + std::to_string(r.Width()) +
                   ", height=" + std::to_string(r.Height()) + ")";
        });
    BindArrayProtocols(raster);
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

RasterGrid CenteredGrid(std::size_t width, std::size_t height, double pixelSize,
                        const Vec2d& center) {
    const double halfWidth = 0.5 * static_cast<double>(width) * pixelSize;
    const double halfHeight = 0.5 * static_cast<double>(height) * pixelSize;
    return RasterGrid{
        width, height,
        GeoTransform::NorthUp(center.x - halfWidth, center.y + halfHeight, pixelSize)};
}

void BindGrid(nb::module_& m) {
    nb::class_<RasterGrid>(m, "RasterGrid",
                           "A `width` x `height` grid of cells placed by a geotransform.")
        .def(
            "__init__",
            [](RasterGrid* self, std::size_t width, std::size_t height, GeoTransform transform) {
                new (self) RasterGrid{width, height, transform};
            },
            "width"_a, "height"_a, "transform"_a)
        .def_static("centered", &CenteredGrid, "width"_a, "height"_a, "pixel_size"_a,
                    "center"_a = Vec2d{}, "A north-up grid of square cells centered on `center`.")
        .def_static("from_raster", &RasterGrid::FromRaster, "raster"_a,
                    "The grid `raster` covers: its size and transform.")
        .def_rw("width", &RasterGrid::width)
        .def_rw("height", &RasterGrid::height)
        .def_rw("transform", &RasterGrid::transform)
        .def_prop_ro("bounds", &RasterGrid::Bounds)
        .def_prop_ro("area", &RasterGrid::Area, "Area in square world units.")
        .def_prop_ro("pixel_size", &RasterGrid::PixelSize, "Cell size in world units.")
        .def("surface_area", &RasterGrid::SurfaceArea, "surface"_a,
             "The area the grid covers on `surface`, in square meters.")
        .def("spacing", &RasterGrid::Spacing, "surface"_a,
             "The sample spacing in meters on `surface`.")
        .def("make_raster", &RasterGrid::MakeRaster, "fill"_a = 0.0f,
             "A raster on this grid filled with `fill`.");
}

void BindResampling(nb::module_& m) {
    BindEnum<ResampleMethod>(m, "ResampleMethod", "How a raster is interpolated.");
    m.def("sample_raster", &SampleRaster, "raster"_a, "x"_a, "y"_a,
          "method"_a = ResampleMethod::Cubic,
          "The raster interpolated at world position (x, y); NaN outside it.");
}

void BindTiffEnums(nb::module_& m) {
    BindEnum<TiffCompression>(m, "TiffCompression", "TIFF compression.");
    nb::enum_<TiffSampleFormat>(m, "TiffSampleFormat", "How a TIFF's samples are stored.")
        .value("UNSIGNED_INTEGER", TiffSampleFormat::UnsignedInteger)
        .value("SIGNED_INTEGER", TiffSampleFormat::SignedInteger)
        .value("FLOAT", TiffSampleFormat::Float);
}

void BindGeoKeys(nb::module_& m) {
    nb::class_<GeoKeys>(m, "GeoKeys", "A GeoTIFF's projection keys (its CRS), as read or to write.")
        .def(nb::init<>(), "No keys: no CRS.")
        .def_prop_ro("empty", &GeoKeys::Empty)
        .def_prop_ro("is_geographic", &GeoKeys::IsGeographic,
                     "Whether the CRS is geographic (degrees of longitude and latitude).")
        .def_prop_ro("pixel_is_point", &GeoKeys::PixelIsPoint)
        .def(nb::self == nb::self);
    m.def("geographic_sphere_keys", &GeographicSphereKeys, "radius"_a,
          "GeoKeys for longitude and latitude on a sphere of `radius` meters, such as a moon or "
          "planet.");
    nb::class_<BandScaling>(m, "BandScaling", "A band's scale and offset: value * scale + offset.")
        .def_ro("scale", &BandScaling::scale)
        .def_ro("offset", &BandScaling::offset);
}

void BindGeoTiffInfo(nb::module_& m) {
    nb::class_<GeoTiffInfo>(m, "GeoTiffInfo", "What a GeoTIFF's header says.")
        .def_ro("width", &GeoTiffInfo::width)
        .def_ro("height", &GeoTiffInfo::height)
        .def_ro("bands", &GeoTiffInfo::bands)
        .def_ro("bits_per_sample", &GeoTiffInfo::bitsPerSample)
        .def_ro("sample_format", &GeoTiffInfo::sampleFormat)
        .def_ro("compression", &GeoTiffInfo::compression)
        .def_ro("predictor", &GeoTiffInfo::predictor)
        .def_ro("tiled", &GeoTiffInfo::tiled)
        .def_ro("big_tiff", &GeoTiffInfo::bigTiff)
        .def_ro("big_endian", &GeoTiffInfo::bigEndian)
        .def_ro("transform", &GeoTiffInfo::transform)
        .def_ro("georeferenced", &GeoTiffInfo::georeferenced)
        .def_ro("no_data", &GeoTiffInfo::noData)
        .def_ro("band_scaling", &GeoTiffInfo::bandScaling)
        .def_ro("geo_keys", &GeoTiffInfo::geoKeys);
    m.def("read_geotiff_info", &ReadGeoTiffInfo, "path"_a,
          "Reads a GeoTIFF's header: size, bands, sample format, compression, georeferencing and "
          "CRS keys.");
}

void BindGeoTiffIo(nb::module_& m) {
    auto options = nb::class_<GeoTiffWriteOptions>(
        m, "GeoTiffWriteOptions",
        "How GeoTIFFs are written: `compression` (none, LZW or Deflate), square tiles of "
        "`tile_size` (a positive multiple of 16) and `big_tiff`.");
    BindKwargsInit<GeoTiffWriteOptions>(options);
    options.def_rw("compression", &GeoTiffWriteOptions::compression)
        .def_rw("tile_size", &GeoTiffWriteOptions::tileSize)
        .def_rw("big_tiff", &GeoTiffWriteOptions::bigTiff);
    using WriteBands = void (*)(const std::filesystem::path&, const std::vector<RasterF>&,
                                const GeoKeys&, const GeoTiffWriteOptions&);
    using WriteOne = void (*)(const std::filesystem::path&, const RasterF&, const GeoKeys&,
                              const GeoTiffWriteOptions&);
    m.def("write_geotiff", static_cast<WriteBands>(&WriteGeoTiff), "path"_a, "bands"_a,
          "geo_keys"_a = GeoKeys{}, "options"_a = GeoTiffWriteOptions{},
          "Writes one or more rasters as the bands of a float32 GeoTIFF, with `geo_keys` as its "
          "CRS.");
    m.def("write_geotiff", static_cast<WriteOne>(&WriteGeoTiff), "path"_a, "raster"_a,
          "geo_keys"_a = GeoKeys{}, "options"_a = GeoTiffWriteOptions{});
    m.def(
        "read_geotiff",
        [](const std::filesystem::path& path, std::size_t band) {
            return ReadGeoTiff(path, band).raster;
        },
        "path"_a, "band"_a = 0,
        "Reads band `band` (from 0) of a GeoTIFF as a Raster with the file's transform and no-data "
        "value; `read_geotiff_info` gives its CRS keys.");
}

}  // namespace

void BindRaster(nb::module_& m) {
    BindGeoTransform(m);
    BindRasterClass(m);
    BindStats(m);
    BindGrid(m);
    BindResampling(m);
    nb::exception<GeoTiffError>(m, "GeoTiffError", PyExc_RuntimeError);
    BindTiffEnums(m);
    BindGeoKeys(m);
    BindGeoTiffInfo(m);
    BindGeoTiffIo(m);
}

}  // namespace terraingen::python
