/// @file
/// @brief TIFF and GeoTIFF numbers shared by the GeoTIFF reader and writer.
#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/util/bits.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace terraingen::detail {

static_assert(kLittleEndian, "The GeoTIFF reader and writer assume a little-endian host");

/// TIFF tags read or written.
enum Tag : std::uint16_t {
    kImageWidth = 256,
    kImageLength = 257,
    kBitsPerSample = 258,
    kCompression = 259,
    kPhotometric = 262,
    kStripOffsets = 273,
    kSamplesPerPixel = 277,
    kRowsPerStrip = 278,
    kStripByteCounts = 279,
    kPlanarConfiguration = 284,
    kPredictor = 317,
    kTileWidth = 322,
    kTileLength = 323,
    kTileOffsets = 324,
    kTileByteCounts = 325,
    kExtraSamples = 338,
    kSampleFormat = 339,
    kModelPixelScale = 33550,
    kModelTiepoint = 33922,
    kModelTransformation = 34264,
    kGeoKeyDirectory = 34735,
    kGeoDoubleParams = 34736,
    kGeoAsciiParams = 34737,
    kGdalMetadata = 42112,
    kGdalNoData = 42113,
};

/// TIFF field types.
enum FieldType : std::uint16_t {
    kByte = 1,
    kAscii = 2,
    kShort = 3,
    kLong = 4,
    kRational = 5,
    kSByte = 6,
    kUndefined = 7,
    kSShort = 8,
    kSLong = 9,
    kSRational = 10,
    kFloat = 11,
    kDouble = 12,
    kLong8 = 16,
    kSLong8 = 17,
    kIfd8 = 18,
};

/// Values of the Compression tag.
enum CompressionCode : std::uint16_t {
    kCompressionNone = 1,
    kCompressionLzw = 5,
    kCompressionDeflate = 8,
    kCompressionAdobeDeflate = 32946,
    kCompressionPackBits = 32773,
};

/// GeoKey numbers from OGC 19-008r4.
enum GeoKey : std::uint16_t {
    kGtModelTypeGeoKey = 1024,
    kGtRasterTypeGeoKey = 1025,
    kGeographicTypeGeoKey = 2048,
    kGeogGeodeticDatumGeoKey = 2050,
    kGeogAngularUnitsGeoKey = 2054,
    kGeogEllipsoidGeoKey = 2056,
    kGeogSemiMajorAxisGeoKey = 2057,
    kGeogSemiMinorAxisGeoKey = 2058,
};

constexpr std::uint16_t kModelTypeGeographic = 2;  ///< GTModelTypeGeoKey value.
constexpr std::uint16_t kRasterPixelIsArea = 1;    ///< GTRasterTypeGeoKey value.
constexpr std::uint16_t kRasterPixelIsPoint = 2;   ///< GTRasterTypeGeoKey value.
constexpr std::uint16_t kUserDefined = 32767;      ///< A user-defined CRS, datum or ellipsoid.
constexpr std::uint16_t kAngularDegree = 9102;     ///< GeogAngularUnitsGeoKey value.

/// The size in bytes of one value of a TIFF field type code, or 0 for an unknown type.
std::size_t FieldSize(std::uint16_t type);

/// The compression a Compression tag value names, or nothing for a scheme this library does not
/// decode.
std::optional<TiffCompression> CompressionFromCode(std::uint64_t code);

/// The Compression tag value written for `compression`; Deflate is written as code 8.
std::uint16_t TiffCompressionCode(TiffCompression compression);

/// The sizes of IFD fields in classic TIFF or BigTIFF.
struct IfdFormat {
    std::size_t countSize;  ///< Size of the entry count before the entries.
    std::size_t wordSize;   ///< Size of a value count, an inline value and an offset.

    /// The size in bytes of one entry: tag, type, count and value or offset.
    constexpr std::size_t EntrySize() const {
        return 4 + 2 * wordSize;
    }

    /// The byte offset within an entry where its value or offset starts.
    constexpr std::size_t ValueAt() const {
        return 4 + wordSize;
    }
};

/// The IFD format of classic TIFF (`bigTiff` false) or BigTIFF.
constexpr IfdFormat IfdLayout(bool bigTiff) {
    return bigTiff ? IfdFormat{8, 8} : IfdFormat{2, 4};
}

/// One IFD entry with its value bytes, already read from wherever they are stored.
struct IfdEntry {
    std::uint16_t type = 0;           ///< Field type (`FieldType`).
    std::uint64_t count = 0;          ///< Number of values.
    std::vector<std::uint8_t> bytes;  ///< The values, in the file's byte order.
};

/// `transform` shifted by half a pixel along both pixel axes.
/// @param sign +1 to shift forward, -1 back.
GeoTransform ShiftHalfPixel(const GeoTransform& transform, double sign);

/// The GeoTIFF model tags that store a pixel-to-world transform: ModelPixelScale and
/// ModelTiepoint for one without rotation, otherwise ModelTransformation. Tags not stored are
/// empty.
struct ModelTags {
    std::vector<double> pixelScale;      ///< ModelPixelScale: x and y pixel size, and z.
    std::vector<double> tiepoint;        ///< ModelTiepoint: one pixel (i, j, k) and world point.
    std::vector<double> transformation;  ///< ModelTransformation: a 4x4 matrix, row by row.
};

/// The model tags storing `transform`. GeoTIFF places PixelIsPoint rasters at their first
/// pixel's center, so `transform` is shifted half a pixel for them.
ModelTags EncodeTransform(const GeoTransform& transform, bool pixelIsPoint);

/// The transform `tags` store, undoing the PixelIsPoint shift; nothing when they store none
/// (neither ModelTransformation nor both ModelPixelScale and ModelTiepoint).
/// @throws GeoTiffError for tags with too few values or several tiepoints.
std::optional<GeoTransform> DecodeTransform(const ModelTags& tags, bool pixelIsPoint);

/// Throws a `GeoTiffError` "<path>: <reason>" for a file read or written.
[[noreturn]] void ThrowFileError(const std::filesystem::path& path, const std::string& reason);

/// How many strips or tiles the GeoTIFF reader has decoded on the calling thread, for tests of
/// windowed reads.
std::size_t DecodedChunkCount();

/// The value of the SHORT GeoKey `key` stored inline in the directory of `keys`, if present.
std::optional<std::uint16_t> ShortGeoKey(const GeoKeys& keys, std::uint16_t key);

/// The band scaling a GDAL_METADATA tag (42112) gives, one per band; scale 1 and offset 0 for
/// bands not mentioned, nor for items without a band, beyond `bands` or not a number.
std::vector<BandScaling> ParseGdalBandScaling(const std::string& xml, std::size_t bands);

}  // namespace terraingen::detail
