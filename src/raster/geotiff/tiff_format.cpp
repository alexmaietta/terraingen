// SPDX-License-Identifier: BSD-3-Clause

#include "raster/geotiff/tiff_format.h"

namespace terraingen::detail {

void ThrowFileError(const std::filesystem::path& path, const std::string& reason) {
    throw GeoTiffError(path.string() + ": " + reason);
}

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

std::uint16_t TiffCompressionCode(TiffCompression compression) {
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

ModelTags EncodeTransform(const GeoTransform& transform, bool pixelIsPoint) {
    const GeoTransform stored = pixelIsPoint ? ShiftHalfPixel(transform, 1.0) : transform;
    ModelTags tags;
    if (!stored.IsRotated()) {
        tags.pixelScale = {stored.pixelWidth, -stored.pixelHeight, 0.0};
        tags.tiepoint = {0.0, 0.0, 0.0, stored.originX, stored.originY, 0.0};
    } else {
        tags.transformation = {stored.pixelWidth,
                               stored.rowRotation,
                               0.0,
                               stored.originX,
                               stored.colRotation,
                               stored.pixelHeight,
                               0.0,
                               stored.originY,
                               0.0,
                               0.0,
                               0.0,
                               0.0,
                               0.0,
                               0.0,
                               0.0,
                               1.0};
    }
    return tags;
}

namespace {

/// The transform the model tags store as GeoTIFF places it; nothing when they store none.
std::optional<GeoTransform> StoredTransform(const ModelTags& tags) {
    if (!tags.transformation.empty()) {
        const std::vector<double>& m = tags.transformation;
        if (m.size() < 16) {
            throw GeoTiffError("ModelTransformation needs 16 values");
        }
        return GeoTransform::FromGdal({m[3], m[0], m[1], m[7], m[4], m[5]});
    }
    if (tags.pixelScale.empty() || tags.tiepoint.empty()) {
        return std::nullopt;
    }
    const std::vector<double>& s = tags.pixelScale;
    const std::vector<double>& t = tags.tiepoint;
    if (s.size() < 2 || t.size() < 6) {
        throw GeoTiffError("ModelPixelScale needs 2 and ModelTiepoint 6 values");
    }
    if (t.size() > 6) {
        throw GeoTiffError("multiple tiepoints (ground control points) are not supported");
    }
    return GeoTransform::FromGdal({t[3] - t[0] * s[0], s[0], 0.0, t[4] + t[1] * s[1], 0.0, -s[1]});
}

}  // namespace

std::optional<GeoTransform> DecodeTransform(const ModelTags& tags, bool pixelIsPoint) {
    const std::optional<GeoTransform> stored = StoredTransform(tags);
    if (stored && pixelIsPoint) {
        return ShiftHalfPixel(*stored, -1.0);
    }
    return stored;
}

}  // namespace terraingen::detail
