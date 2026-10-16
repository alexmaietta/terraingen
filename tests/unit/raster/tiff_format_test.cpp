#include "terraingen/raster/tiff_format.h"

#include "terraingen/raster/tiff_codecs.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using terraingen::GeoTransform;
using terraingen::TiffCompression;
using terraingen::detail::CompressionCodeOf;
using terraingen::detail::CompressionFromCode;
using terraingen::detail::FieldSize;
using terraingen::detail::IfdFormat;
using terraingen::detail::kAscii;
using terraingen::detail::kFloat;
using terraingen::detail::kIfd8;
using terraingen::detail::kSShort;
using terraingen::detail::LzwNextWidth;
using terraingen::detail::ShiftHalfPixel;

TEST(TiffFormat, IfdFieldSizesForClassicAndBigTiff) {
    const IfdFormat classic = IfdFormat::For(false);
    EXPECT_EQ(classic.countSize, 2u);
    EXPECT_EQ(classic.wordSize, 4u);
    EXPECT_EQ(classic.EntrySize(), 12u);
    EXPECT_EQ(classic.ValueAt(), 8u);
    const IfdFormat big = IfdFormat::For(true);
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
        EXPECT_EQ(CompressionFromCode(CompressionCodeOf(compression)), compression);
    }
    EXPECT_EQ(CompressionCodeOf(TiffCompression::Deflate), 8);
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
