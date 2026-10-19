#include "terraingen/raster/geotiff/tiff_format.h"

#include "terraingen/raster/geotiff/tiff_codecs.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using terraingen::GeoTransform;
using terraingen::TiffCompression;
using terraingen::detail::CompressionFromCode;
using terraingen::detail::FieldSize;
using terraingen::detail::IfdFormat;
using terraingen::detail::kAscii;
using terraingen::detail::kFloat;
using terraingen::detail::kIfd8;
using terraingen::detail::kSShort;
using terraingen::detail::LzwNextWidth;
using terraingen::detail::ShiftHalfPixel;
using terraingen::detail::TiffCompressionCode;

TEST(TiffFormat, IfdFieldSizesForClassicAndBigTiff) {
    const IfdFormat classic = terraingen::detail::IfdLayout(false);
    EXPECT_EQ(classic.countSize, 2u);
    EXPECT_EQ(classic.wordSize, 4u);
    EXPECT_EQ(classic.EntrySize(), 12u);
    EXPECT_EQ(classic.ValueAt(), 8u);
    const IfdFormat big = terraingen::detail::IfdLayout(true);
    EXPECT_EQ(big.countSize, 8u);
    EXPECT_EQ(big.EntrySize(), 20u);
    EXPECT_EQ(big.ValueAt(), 12u);
}

TEST(TiffFormat, FieldSizes) {
    EXPECT_EQ(FieldSize(kAscii), 1u);
    EXPECT_EQ(FieldSize(kSShort), 2u);
    EXPECT_EQ(FieldSize(kFloat), 4u);
    EXPECT_EQ(FieldSize(kIfd8), 8u);
    EXPECT_EQ(FieldSize(99), 0u);
}

TEST(TiffFormat, CompressionCodesRoundTrip) {
    for (const TiffCompression compression :
         {TiffCompression::None, TiffCompression::Lzw, TiffCompression::PackBits,
          TiffCompression::Deflate}) {
        EXPECT_EQ(CompressionFromCode(TiffCompressionCode(compression)), compression);
    }
    EXPECT_EQ(TiffCompressionCode(TiffCompression::Deflate), 8);
    EXPECT_EQ(CompressionFromCode(32946), TiffCompression::Deflate);
    EXPECT_FALSE(CompressionFromCode(7).has_value());
}

TEST(TiffFormat, ShiftHalfPixelMovesTheOriginAlongBothPixelAxes) {
    const GeoTransform transform = GeoTransform::FromGdal({10.0, 2.0, 0.5, 20.0, 0.25, -4.0});
    const GeoTransform shifted = ShiftHalfPixel(transform, 1.0);
    EXPECT_DOUBLE_EQ(shifted.originX, 10.0 + 0.5 * (2.0 + 0.5));
    EXPECT_DOUBLE_EQ(shifted.originY, 20.0 + 0.5 * (0.25 - 4.0));
    EXPECT_EQ(ShiftHalfPixel(shifted, -1.0), transform);
}

TEST(TiffFormat, TransformsRoundTripThroughTheModelTags) {
    using terraingen::detail::DecodeTransform;
    using terraingen::detail::EncodeTransform;
    const GeoTransform northUp = GeoTransform::NorthUp(-10.5, 20.25, 0.5);
    const GeoTransform rotated = GeoTransform::FromGdal({10.0, 2.0, 0.5, 20.0, 0.25, -4.0});
    for (const bool pixelIsPoint : {false, true}) {
        const auto scaled = EncodeTransform(northUp, pixelIsPoint);
        EXPECT_EQ(scaled.pixelScale.size(), 3u);
        EXPECT_EQ(scaled.tiepoint.size(), 6u);
        EXPECT_TRUE(scaled.transformation.empty());
        EXPECT_EQ(DecodeTransform(scaled, pixelIsPoint), northUp);
        const auto matrix = EncodeTransform(rotated, pixelIsPoint);
        EXPECT_TRUE(matrix.pixelScale.empty());
        EXPECT_EQ(matrix.transformation.size(), 16u);
        EXPECT_EQ(DecodeTransform(matrix, pixelIsPoint), rotated);
    }
    EXPECT_DOUBLE_EQ(EncodeTransform(northUp, true).tiepoint[3], -10.5 + 0.25);
    EXPECT_FALSE(DecodeTransform({}, false).has_value());
    EXPECT_FALSE(DecodeTransform({{1.0, 1.0, 0.0}, {}, {}}, false).has_value());
}

TEST(TiffFormat, DecodeTransformRejectsShortOrSeveralTiepoints) {
    using terraingen::GeoTiffError;
    using terraingen::detail::DecodeTransform;
    EXPECT_THROW(DecodeTransform({{}, {}, {1.0, 0.0, 0.0}}, false), GeoTiffError);
    EXPECT_THROW(DecodeTransform({{1.0}, {0, 0, 0, 0, 0, 0}, {}}, false), GeoTiffError);
    EXPECT_THROW(DecodeTransform({{1.0, 1.0}, std::vector<double>(12, 0.0), {}}, false),
                 GeoTiffError);
}

TEST(TiffFormat, GeographicSphereKeysDirectory) {
    const auto keys = terraingen::GeographicSphereKeys(2.0);
    const std::vector<std::uint16_t> expected = {
        1,    1, 0, 8,     1024, 0,     1, 2,     1025, 0,     1, 1,
        2048, 0, 1, 32767, 2050, 0,     1, 32767, 2054, 0,     1, 9102,
        2056, 0, 1, 32767, 2057, 34736, 1, 0,     2058, 34736, 1, 1};
    EXPECT_EQ(keys.directory, expected);
}

TEST(TiffCodecs, LzwWidthGrowsOneCodeEarlyUpToTwelveBits) {
    EXPECT_EQ(LzwNextWidth(511, 9), 9);
    EXPECT_EQ(LzwNextWidth(512, 9), 10);
    EXPECT_EQ(LzwNextWidth(1024, 10), 11);
    EXPECT_EQ(LzwNextWidth(4096, 12), 12);
}

TEST(TiffCodecs, FloatPredictorRoundTripsAndGroupsBytePlanes) {
    const std::vector<float> samples{1.5f, -2.25f, 1e6f, 0.0f, 3.0f, 3.0f};
    for (const std::size_t stride : {1u, 2u}) {
        std::vector<std::uint8_t> row(samples.size() * 4);
        std::memcpy(row.data(), samples.data(), row.size());
        const std::vector<std::uint8_t> original = row;
        terraingen::detail::EncodeFloatPredictorRow(row.data(), 4, samples.size(), stride);
        EXPECT_NE(row, original);
        if (stride == 1) {
            EXPECT_EQ(row[0], original[3]) << "the first plane holds the most significant bytes";
        }
        terraingen::detail::DecodeFloatPredictorRow(row.data(), 4, samples.size(), stride);
        EXPECT_EQ(row, original);
    }
}
