#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraingen {

/// A GeoTIFF that cannot be read or written, with the reason.
class GeoTiffError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// A GeoTIFF's projection, kept as the raw GeoKey tags so it can be written back unchanged.
///
/// TerrainGen does not interpret projections: a raster read from a GeoTIFF keeps its CRS by
/// carrying these keys to the GeoTIFF it is written to, while the pixel-to-world transform travels
/// in the raster's `GeoTransform`. Reprojection needs a GDAL-backed reader.
///
/// Reference: OGC GeoTIFF Standard 1.1, OGC 19-008r4, 2019.
/// https://docs.ogc.org/is/19-008r4/19-008r4.html
struct GeoKeys {
    std::vector<std::uint16_t> directory;  ///< GeoKeyDirectoryTag (34735).
    std::vector<double> doubleParams;      ///< GeoDoubleParamsTag (34736).
    std::string asciiParams;               ///< GeoAsciiParamsTag (34737).

    bool Empty() const {
        return directory.empty();
    }

    /// Whether GTRasterTypeGeoKey says pixel coordinates name pixel centres (PixelIsPoint) rather
    /// than pixel areas.
    bool PixelIsPoint() const;

    bool operator==(const GeoKeys&) const = default;
};

/// Compression schemes this library reads and writes. Others, such as Deflate, JPEG and ZSTD, are
/// reported as unsupported; `gdal_translate -co COMPRESS=LZW` converts them.
enum class TiffCompression {
    None,      ///< Code 1.
    Lzw,       ///< Code 5, with TIFF 6.0's early code-width change.
    PackBits,  ///< Code 32773; read only.
};

/// How sample bits are interpreted.
enum class TiffSampleFormat { UnsignedInteger, SignedInteger, Float };

/// The layout and georeferencing of a GeoTIFF's first image.
struct GeoTiffInfo {
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t bands = 0;
    int bitsPerSample = 0;
    TiffSampleFormat sampleFormat = TiffSampleFormat::UnsignedInteger;
    TiffCompression compression = TiffCompression::None;
    int predictor = 1;  ///< 1 none, 2 horizontal differencing, 3 floating point.
    bool tiled = false;
    bool bigTiff = false;
    bool bigEndian = false;
    /// Pixel-to-world transform, the identity when the file has none; with PixelIsPoint the
    /// origin is shifted half a pixel so it names the corner of the first pixel, as GDAL does.
    GeoTransform transform{};
    bool georeferenced = false;
    std::optional<double> noData;  ///< From the GDAL_NODATA tag (42113).
    GeoKeys geoKeys;
};

/// Reads the layout and georeferencing of the GeoTIFF at `path` without reading pixels.
///
/// Classic TIFF and BigTIFF, in either byte order, are supported; only the first image is read,
/// which is the full-resolution image of a cloud-optimized GeoTIFF.
///
/// References:
/// - Aldus Corporation, "TIFF Revision 6.0", 1992.
///   https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf
/// - BigTIFF file format, LibTIFF documentation.
///   https://libtiff.gitlab.io/libtiff/specification/bigtiff.html
/// - OGC GeoTIFF Standard 1.1, OGC 19-008r4, 2019.
///   https://docs.ogc.org/is/19-008r4/19-008r4.html
/// - GDAL GTiff driver: GDAL_NODATA tag and PixelIsPoint handling.
///   https://gdal.org/en/stable/drivers/raster/gtiff.html
/// @throws GeoTiffError for unreadable or malformed files.
GeoTiffInfo ReadGeoTiffInfo(const std::filesystem::path& path);

/// A raster read from a GeoTIFF with the projection keys to write it back with.
struct GeoTiff {
    Raster<float> raster;
    GeoKeys geoKeys;
};

/// Reads band `band` (from 0) of the GeoTIFF at `path` as heights.
///
/// Strips and tiles, chunky and planar band layouts, 8- to 64-bit integer and 32- and 64-bit float
/// samples, no compression, LZW and PackBits, and horizontal and floating-point predictors are
/// supported. Samples are converted to `float`. The raster gets the file's transform and no-data
/// value; the GeoKeys are returned alongside. See `ReadGeoTiffInfo` for the specifications.
///
/// Reference for the floating-point predictor: Adobe, "Adobe Photoshop TIFF Technical Note 3",
/// 2005. http://chriscox.org/TIFFTN3d1.pdf
/// @throws GeoTiffError for unreadable, malformed or unsupported files, or a missing band.
GeoTiff ReadGeoTiff(const std::filesystem::path& path, std::size_t band = 0);

/// Settings for `WriteGeoTiff`.
struct GeoTiffWriteOptions {
    /// `None` or `Lzw`; LZW uses the floating-point predictor, which suits elevation data.
    TiffCompression compression = TiffCompression::Lzw;
    /// Tile width and height in pixels; a positive multiple of 16.
    std::uint32_t tileSize = 256;
    /// Always write BigTIFF; otherwise BigTIFF is used only when the file could exceed 4 GiB.
    bool bigTiff = false;
};

/// Writes `raster` as a tiled, little-endian float32 GeoTIFF.
///
/// The raster's transform is written as ModelPixelScale and ModelTiepoint (or
/// ModelTransformation when it is rotated), shifted back half a pixel when `geoKeys` say
/// PixelIsPoint, so a raster read with `ReadGeoTiff`, resampled and written with its `geoKeys`
/// keeps its projection. Georeferencing is omitted for a raster with the identity transform and no
/// keys. The no-data value is written as GDAL_NODATA. Edge tiles are padded with the no-data value,
/// or 0 without one.
/// @throws GeoTiffError for an empty raster, invalid options, or a file that cannot be written.
void WriteGeoTiff(const std::filesystem::path& path, const Raster<float>& raster,
                  const GeoKeys& geoKeys = {}, const GeoTiffWriteOptions& options = {});

}  // namespace terraingen
