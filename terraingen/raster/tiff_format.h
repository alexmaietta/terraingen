/// @file
/// @brief TIFF and GeoTIFF numbers shared by the GeoTIFF reader and writer.
#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/geotiff.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen::detail {

static_assert(std::endian::native == std::endian::little,
              "The GeoTIFF reader and writer assume a little-endian host");

/// @brief TIFF tags read or written.
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

/// @brief TIFF field types.
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

/// @brief Values of the Compression tag.
enum CompressionCode : std::uint16_t {
    kCompressionNone = 1,
    kCompressionLzw = 5,
    kCompressionDeflate = 8,
    kCompressionAdobeDeflate = 32946,
    kCompressionPackBits = 32773,
};

/// @brief GeoKey numbers from OGC 19-008r4.
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

/// @brief The size of one value of a field type.
/// @param type A TIFF field type code.
/// @return The size in bytes, or 0 for an unknown type.
std::size_t FieldSize(std::uint16_t type);

/// @brief The compression a Compression tag value names.
/// @param code The tag value.
/// @return The compression, or nothing for a scheme this library does not decode.
std::optional<TiffCompression> CompressionFromCode(std::uint64_t code);

/// @brief The Compression tag value written for `compression`.
/// @param compression A compression this library writes or reads.
/// @return The tag value; Deflate is written as code 8.
std::uint16_t CompressionCodeOf(TiffCompression compression);

/// @brief The sizes of IFD fields in classic TIFF or BigTIFF.
struct IfdFormat {
    std::size_t countSize;  ///< Size of the entry count before the entries.
    std::size_t wordSize;   ///< Size of a value count, an inline value and an offset.

    /// @brief The format of classic TIFF (`bigTiff` false) or BigTIFF.
    /// @param bigTiff Whether the file is BigTIFF.
    /// @return The field sizes.
    static constexpr IfdFormat For(bool bigTiff) {
        return bigTiff ? IfdFormat{8, 8} : IfdFormat{2, 4};
    }

    /// @brief The size of one entry: tag, type, count and value or offset.
    /// @return The size in bytes.
    constexpr std::size_t EntrySize() const {
        return 4 + 2 * wordSize;
    }

    /// @brief Where an entry's value or offset starts within the entry.
    /// @return The offset in bytes.
    constexpr std::size_t ValueAt() const {
        return 4 + wordSize;
    }
};

/// @brief One IFD entry with its value bytes, already read from wherever they are stored.
struct Entry {
    std::uint16_t type = 0;           ///< Field type (`FieldType`).
    std::uint64_t count = 0;          ///< Number of values.
    std::vector<std::uint8_t> bytes;  ///< The values, in the file's byte order.
};

/// @brief `transform` shifted by half a pixel along both pixel axes.
/// @param transform The transform to shift.
/// @param sign +1 to shift forward, -1 back.
/// @return The shifted transform.
GeoTransform ShiftHalfPixel(const GeoTransform& transform, double sign);

}  // namespace terraingen::detail
