#include "terraingen/raster/tiff_format.h"

namespace terraingen::detail {

std::size_t FieldSize(std::uint16_t type) {
    switch (type) {
    case kByte:
    case kAscii:
    case kSByte:
    case kUndefined:
        return 1;
    case kShort:
    case kSShort:
        return 2;
    case kLong:
    case kSLong:
    case kFloat:
        return 4;
    case kRational:
    case kSRational:
    case kDouble:
    case kLong8:
    case kSLong8:
    case kIfd8:
        return 8;
    default:
        return 0;
    }
}

std::optional<TiffCompression> CompressionFromCode(std::uint64_t code) {
    switch (code) {
    case kCompressionNone:
        return TiffCompression::None;
    case kCompressionLzw:
        return TiffCompression::Lzw;
    case kCompressionPackBits:
        return TiffCompression::PackBits;
    case kCompressionDeflate:
    case kCompressionAdobeDeflate:
        return TiffCompression::Deflate;
    default:
        return std::nullopt;
    }
}

std::uint16_t CompressionCodeOf(TiffCompression compression) {
    switch (compression) {
    case TiffCompression::Lzw:
        return kCompressionLzw;
    case TiffCompression::PackBits:
        return kCompressionPackBits;
    case TiffCompression::Deflate:
        return kCompressionDeflate;
    default:
        return kCompressionNone;
    }
}

GeoTransform ShiftHalfPixel(const GeoTransform& transform, double sign) {
    GeoTransform shifted = transform;
    shifted.originX += sign * 0.5 * (transform.pixelWidth + transform.rowRotation);
    shifted.originY += sign * 0.5 * (transform.colRotation + transform.pixelHeight);
    return shifted;
}

}  // namespace terraingen::detail
