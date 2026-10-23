// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Reading and writing GeoTIFF rasters, with their projection keys and band scaling.
#pragma once

#include "terraingen/raster/cell_loops.h"
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

/// A GeoTIFF's projection as raw GeoKey tags, written back unchanged.
///
/// Projections are not interpreted.
struct GeoKeys {
    std::vector<std::uint16_t> directory;  ///< GeoKeyDirectoryTag (34735).
    std::vector<double> doubleParams;      ///< GeoDoubleParamsTag (34736).
    std::string asciiParams;               ///< GeoAsciiParamsTag (34737).

    /// Whether there are no keys (the directory is empty).
    bool Empty() const {
        return directory.empty();
    }

    /// Whether GTRasterTypeGeoKey is PixelIsPoint: pixel coordinates name pixel centers rather
    /// than pixel areas.
    bool PixelIsPoint() const;

    /// Whether GTModelTypeGeoKey is geographic: coordinates are angles rather than a projection's
    /// linear units.
    bool IsGeographic() const;

    /// Memberwise equality.
    bool operator==(const GeoKeys& other) const {
        return directory == other.directory && doubleParams == other.doubleParams &&
               asciiParams == other.asciiParams;
    }
    bool operator!=(const GeoKeys& other) const {
        return !(*this == other);
    }
};

/// Compression schemes this library reads and writes; others are reported as unsupported.
enum class TiffCompression {
    None,      ///< Code 1.
    Lzw,       ///< Code 5, with TIFF 6.0's early code-width change.
    PackBits,  ///< Code 32773; read only.
    Deflate,   ///< Codes 8 and 32946 (zlib streams), decoded and encoded with miniz.
};

/// How sample bits are interpreted.
enum class TiffSampleFormat { UnsignedInteger, SignedInteger, Float };

/// A band's scale and offset: a sample `s` stands for `s * scale + offset`, as GDAL's band
/// SCALE and OFFSET metadata say.
struct BandScaling {
    double scale = 1.0;   ///< Multiplier of the stored sample.
    double offset = 0.0;  ///< Added after scaling.

    /// Whether the scaling leaves samples unchanged (scale 1 and offset 0).
    bool Identity() const {
        return scale == 1.0 && offset == 0.0;
    }

    /// Memberwise equality.
    bool operator==(const BandScaling& other) const {
        return scale == other.scale && offset == other.offset;
    }
    bool operator!=(const BandScaling& other) const {
        return !(*this == other);
    }
};

/// The layout and georeferencing of a GeoTIFF's first image.
struct GeoTiffInfo {
    std::size_t width = 0;   ///< Columns.
    std::size_t height = 0;  ///< Rows.
    std::size_t bands = 0;   ///< Samples per pixel.
    int bitsPerSample = 0;   ///< Bits of each sample.
    /// How samples are stored.
    TiffSampleFormat sampleFormat = TiffSampleFormat::UnsignedInteger;
    /// How the image data is compressed.
    TiffCompression compression = TiffCompression::None;
    int predictor = 1;       ///< 1 none, 2 horizontal differencing, 3 floating point.
    bool tiled = false;      ///< Tiles rather than strips.
    bool bigTiff = false;    ///< BigTIFF rather than classic TIFF.
    bool bigEndian = false;  ///< Big-endian ("MM") byte order.
    /// Pixel-to-world transform, the identity without one; for PixelIsPoint shifted half a pixel
    /// to name the first pixel's corner, as GDAL does.
    GeoTransform transform{};
    bool georeferenced = false;    ///< Whether the file has a transform.
    std::optional<double> noData;  ///< From the GDAL_NODATA tag (42113).
    /// One per band, from the GDAL_METADATA tag (42112); `ReadGeoTiff` returns raw samples.
    std::vector<BandScaling> bandScaling;
    GeoKeys geoKeys;  ///< The projection keys.
};

/// Reads the layout and georeferencing of a GeoTIFF's first image without its pixels.
///
/// Classic TIFF and BigTIFF in either byte order.
/// @throws GeoTiffError for unreadable or malformed files.
GeoTiffInfo ReadGeoTiffInfo(const std::filesystem::path& path);

/// A raster read from a GeoTIFF with the projection keys to write it back with.
struct GeoTiff {
    Raster<float> raster;  ///< The band read, with the file's transform and no-data value.
    GeoKeys geoKeys;       ///< The file's projection keys.
};

/// Reads one band of a GeoTIFF's first image as `float` samples, with the file's transform,
/// no-data value and GeoKeys.
///
/// Strips or tiles, chunky or planar bands, 8- to 64-bit integer and 32- and 64-bit float samples,
/// LZW, Deflate or PackBits, with or without a predictor. Band scaling is not applied.
/// @param band The band, from 0.
/// @throws GeoTiffError for unreadable, malformed or unsupported files, or a missing band.
GeoTiff ReadGeoTiff(const std::filesystem::path& path, std::size_t band = 0);

/// Reads one band of a window of a GeoTIFF's first image as `ReadGeoTiff` reads it, decoding only
/// the strips or tiles that meet the window; the raster's transform starts at the window's first
/// cell.
/// @param band The band, from 0.
/// @param window The cells to read; clipped to the image.
/// @throws GeoTiffError as `ReadGeoTiff`.
/// @throws std::invalid_argument for a window that is empty once clipped.
GeoTiff ReadGeoTiffWindow(const std::filesystem::path& path, std::size_t band,
                          const PixelWindow& window);

/// GeoKeys for longitude and latitude in degrees on a sphere, such as a planet or moon.
///
/// A user-defined geographic CRS with both axes `radius` and pixel-is-area rasters.
/// @param radius The sphere's radius in meters; positive and finite.
/// @throws std::invalid_argument unless `radius` is positive and finite.
GeoKeys GeographicSphereKeys(double radius);

/// Settings for `WriteGeoTiff`.
struct GeoTiffWriteOptions {
    /// `None`, `Lzw` or `Deflate`; compressed files use the floating-point predictor.
    TiffCompression compression = TiffCompression::Lzw;
    /// Tile width and height in pixels; a positive multiple of 16.
    std::uint32_t tileSize = 256;
    /// Always write BigTIFF; otherwise BigTIFF is used only when the file could exceed 4 GiB.
    bool bigTiff = false;
};

/// Writes a raster as a tiled, little-endian float32 GeoTIFF.
///
/// The transform is written as ModelPixelScale and ModelTiepoint, or ModelTransformation when
/// rotated, shifted back half a pixel when `geoKeys` say PixelIsPoint; it is omitted for the
/// identity transform without keys. The no-data value is written as GDAL_NODATA. Edge tiles are
/// padded with the no-data value, or 0 without one.
/// @param geoKeys The projection keys, as `ReadGeoTiff` returned them.
/// @throws GeoTiffError for an empty raster, invalid options, or a file that cannot be written.
void WriteGeoTiff(const std::filesystem::path& path, const Raster<float>& raster,
                  const GeoKeys& geoKeys = {}, const GeoTiffWriteOptions& options = {});

/// Writes rasters as one GeoTIFF of as many float32 bands.
///
/// Several bands are stored band after band (planar configuration 2), with the first band's
/// transform and no-data value; otherwise as `WriteGeoTiff` for one raster.
/// @param bands The bands; at least one, all on one grid.
/// @throws GeoTiffError as for one raster, or for no bands or bands on different grids.
void WriteGeoTiff(const std::filesystem::path& path, const std::vector<Raster<float>>& bands,
                  const GeoKeys& geoKeys = {}, const GeoTiffWriteOptions& options = {});

}  // namespace terraingen
