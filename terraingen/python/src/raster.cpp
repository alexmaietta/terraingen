#include "bindings.h"
#include "options.h"

#include "terraingen/terraingen.h"

#include <nanobind/operators.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

void BindRasterIo(nb::module_& m) {
    nb::exception<GeoTiffError>(m, "GeoTiffError", PyExc_RuntimeError);
    nb::enum_<TiffCompression>(m, "TiffCompression", "TIFF compression.")
        .value("NONE", TiffCompression::None)
        .value("LZW", TiffCompression::Lzw)
        .value("PACKBITS", TiffCompression::PackBits, "Read only.")
        .value("DEFLATE", TiffCompression::Deflate);
    nb::enum_<TiffSampleFormat>(m, "TiffSampleFormat", "How a TIFF's samples are stored.")
        .value("UNSIGNED_INTEGER", TiffSampleFormat::UnsignedInteger)
        .value("SIGNED_INTEGER", TiffSampleFormat::SignedInteger)
        .value("FLOAT", TiffSampleFormat::Float);
    nb::class_<GeoKeys>(m, "GeoKeys", "A GeoTIFF's projection keys (its CRS), as read or to write.")
        .def(nb::init<>(), "No keys: no CRS.")
        .def_prop_ro("empty", &GeoKeys::Empty)
        .def_prop_ro("is_geographic", &GeoKeys::IsGeographic,
                     "Whether the CRS is geographic (degrees of longitude and latitude).")
        .def_prop_ro("pixel_is_point", &GeoKeys::PixelIsPoint)
        .def(nb::self == nb::self);
    m.def("geographic_sphere_keys", &GeographicSphereKeys, "radius"_a,
          "GeoKeys for longitude and latitude on a sphere of `radius` metres, such as a moon or "
          "planet.");
    nb::class_<BandScaling>(m, "BandScaling", "A band's scale and offset: value * scale + offset.")
        .def_ro("scale", &BandScaling::scale)
        .def_ro("offset", &BandScaling::offset);
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
        .def_ro("nodata", &GeoTiffInfo::noData)
        .def_ro("band_scaling", &GeoTiffInfo::bandScaling)
        .def_ro("geo_keys", &GeoTiffInfo::geoKeys);
    m.def("read_geotiff_info", &ReadGeoTiffInfo, "path"_a,
          "Reads a GeoTIFF's header: size, bands, sample format, compression, georeferencing and "
          "CRS keys.");

    auto options = nb::class_<GeoTiffWriteOptions>(
        m, "GeoTiffWriteOptions",
        "How GeoTIFFs are written: `compression` (none, LZW or Deflate), square tiles of "
        "`tile_size` (a positive multiple of 16) and `big_tiff`.");
    BindKwargsInit<GeoTiffWriteOptions>(options);
    options.def_rw("compression", &GeoTiffWriteOptions::compression)
        .def_rw("tile_size", &GeoTiffWriteOptions::tileSize)
        .def_rw("big_tiff", &GeoTiffWriteOptions::bigTiff);
    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const std::vector<const Raster<float>*>& bands,
           const GeoKeys& geoKeys,
           const GeoTiffWriteOptions& options) { WriteGeoTiff(path, bands, geoKeys, options); },
        "path"_a, "bands"_a, "geo_keys"_a = GeoKeys{}, "options"_a = GeoTiffWriteOptions{},
        "Writes one or more rasters as the bands of a float32 GeoTIFF, with `geo_keys` as its "
        "CRS.");
    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const Raster<float>& raster, const GeoKeys& geoKeys,
           const GeoTiffWriteOptions& options) { WriteGeoTiff(path, raster, geoKeys, options); },
        "path"_a, "raster"_a, "geo_keys"_a = GeoKeys{}, "options"_a = GeoTiffWriteOptions{});
    m.def(
        "read_geotiff",
        [](const std::filesystem::path& path, std::size_t band) {
            return ReadGeoTiff(path, band).raster;
        },
        "path"_a, "band"_a = 0,
        "Reads band `band` (from 0) of a GeoTIFF as a Raster with the file's transform and no-data "
        "value; `read_geotiff_info` gives its CRS keys.");

    m.def("gaussian_blur", nb::overload_cast<const Raster<float>&, double>(&GaussianBlur),
          "raster"_a, "sigma_pixels"_a,
          "The raster blurred by a Gaussian of `sigma_pixels` cells; NaN cells "
          "are left out of the weights.");
    m.def("gaussian_blur", nb::overload_cast<const Raster<float>&, double, double>(&GaussianBlur),
          "raster"_a, "sigma_columns"_a, "sigma_rows"_a);
    m.def("resample", &Resample, "raster"_a, "pixel_size"_a, "method"_a = ResampleMethod::Cubic,
          "The raster resampled to square cells of `pixel_size` over the same extent.");
    m.def("resample_to", &ResampleTo, "raster"_a, "grid"_a, "method"_a = ResampleMethod::Cubic,
          "The raster resampled onto `grid`.");
    m.def("sample_raster", &SampleRaster, "raster"_a, "x"_a, "y"_a,
          "method"_a = ResampleMethod::Cubic,
          "The raster interpolated at world position (x, y); NaN outside it.");
}

}  // namespace terraingen::python
