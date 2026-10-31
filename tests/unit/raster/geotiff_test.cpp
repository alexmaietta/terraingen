// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/geotiff/geotiff.h"

#include "raster/geotiff/tiff_codecs.h"
#include "raster/geotiff/tiff_format.h"
#include "support/gdal_fixtures.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using terraingen::GeoKeys;
using terraingen::GeoTiffError;
using terraingen::GeoTiffWriteOptions;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::ReadGeoTiff;
using terraingen::ReadGeoTiffInfo;
using terraingen::TiffCompression;
using terraingen::WriteGeoTiff;

namespace {

using namespace terraingen::testing;  // NOLINT(google-build-using-namespace): the fixtures

std::filesystem::path WriteBytes(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const auto path = TempPath(name);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

/// A test raster with a slope, a ridge and fractional values.
Raster<float> Sample(std::size_t width, std::size_t height,
                     const GeoTransform& transform = GeoTransform::NorthUp(-1000.25, 2500.5, 0.5)) {
    Raster<float> raster(RasterGrid{width, height, transform});
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            raster(col, row) = static_cast<float>(
                0.37 * static_cast<double>(col) - 1.9 * static_cast<double>(row) +
                50.0 * std::sin(0.13 * static_cast<double>(col * row)));
        }
    }
    return raster;
}

std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint8_t> bytes) {
    return bytes;
}

/// The default write options with tiles of `tileSize` pixels.
GeoTiffWriteOptions Tiled(std::uint32_t tileSize) {
    GeoTiffWriteOptions options;
    options.tileSize = tileSize;
    return options;
}

/// `bytes`, a little-endian classic TIFF, with the inline value of tag `tag` in its first IFD set
/// to `value`, or with the tag renumbered to an unused private tag when `remove` is set.
std::vector<std::uint8_t> PatchTag(std::vector<std::uint8_t> bytes, std::uint16_t tag,
                                   std::uint32_t value, bool remove = false) {
    std::uint32_t ifd = 0;
    std::memcpy(&ifd, bytes.data() + 4, 4);
    std::uint16_t count = 0;
    std::memcpy(&count, bytes.data() + ifd, 2);
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint8_t* entry = bytes.data() + ifd + 2 + 12 * i;
        std::uint16_t found = 0;
        std::memcpy(&found, entry, 2);
        if (found != tag) {
            continue;
        }
        if (remove) {
            const std::uint16_t unused = 65000;
            std::memcpy(entry, &unused, 2);
            return bytes;
        }
        std::uint16_t type = 0;
        std::memcpy(&type, entry + 2, 2);
        if (type == 3) {
            const auto shortValue = static_cast<std::uint16_t>(value);
            std::memcpy(entry + 8, &shortValue, 2);
        } else {
            std::memcpy(entry + 8, &value, 4);
        }
        return bytes;
    }
    ADD_FAILURE() << "tag " << tag << " not found";
    return bytes;
}

/// The message of the `GeoTiffError` reading `bytes` throws, or "no error".
std::string ReadError(const std::vector<std::uint8_t>& bytes) {
    try {
        ReadGeoTiff(WriteBytes("patched.tif", bytes));
    } catch (const GeoTiffError& error) {
        return error.what();
    }
    return "no error";
}

}  // namespace

TEST(ReadGeoTiff, MatchesGdalForEveryFixtureLayout) {
    for (const GdalFixture* fixture : kGdalFixtures) {
        SCOPED_TRACE(fixture->name);
        const auto path = WriteBytes(std::string("gdal_") + fixture->name + ".tif", fixture->bytes);
        const auto info = ReadGeoTiffInfo(path);
        EXPECT_EQ(info.grid.width, fixture->width);
        EXPECT_EQ(info.grid.height, fixture->height);
        EXPECT_EQ(info.bands, fixture->bands);
        EXPECT_EQ(info.georeferenced, fixture->georeferenced);
        ASSERT_EQ(info.bandScaling.size(), fixture->bands);
        for (std::size_t band = 0; band < fixture->bands; ++band) {
            EXPECT_EQ(info.bandScaling[band].scale, fixture->scales[band]) << band;
            EXPECT_EQ(info.bandScaling[band].offset, fixture->offsets[band]) << band;
        }
        const auto tiff = ReadGeoTiff(path, fixture->band);
        const Raster<float>& raster = tiff.raster;
        ASSERT_EQ(raster.Size(), fixture->values.size());
        for (std::size_t i = 0; i < raster.Size(); ++i) {
            ASSERT_EQ(raster.Values()[i], fixture->values[i]) << i;
        }
        if (fixture->georeferenced) {
            EXPECT_EQ(raster.Transform(), GeoTransform::FromGdal(fixture->transform));
            EXPECT_FALSE(tiff.geoKeys.Empty());
        } else {
            EXPECT_EQ(raster.Transform(), GeoTransform{});
        }
        if (fixture->noData) {
            ASSERT_TRUE(raster.NoData().has_value());
            EXPECT_EQ(*raster.NoData(), static_cast<float>(*fixture->noData));
        } else {
            EXPECT_FALSE(raster.NoData().has_value());
        }
    }
}

namespace {

/// The bits of a sample, equal for equal NaNs.
std::uint32_t Bits(float value) {
    return terraingen::BitCast<std::uint32_t>(value);
}

/// Checks that each window read from `path` is the same crop of the whole image, transform and
/// no-data value included.
void ExpectWindowsCropTheImage(const std::filesystem::path& path, std::size_t band,
                               const std::vector<terraingen::PixelWindow>& windows) {
    const auto whole = ReadGeoTiff(path, band);
    const Raster<float>& full = whole.raster;
    for (const terraingen::PixelWindow& window : windows) {
        SCOPED_TRACE(::testing::Message() << window.colBegin << ".." << window.colEnd << " x "
                                          << window.rowBegin << ".." << window.rowEnd);
        const auto part = terraingen::ReadGeoTiffWindow(path, band, window);
        const std::size_t colEnd = std::min(window.colEnd, full.Width());
        const std::size_t rowEnd = std::min(window.rowEnd, full.Height());
        ASSERT_EQ(part.raster.Width(), colEnd - window.colBegin);
        ASSERT_EQ(part.raster.Height(), rowEnd - window.rowBegin);
        const terraingen::RasterGrid expected =
            terraingen::Subgrid(full.Grid(), {window.colBegin, colEnd, window.rowBegin, rowEnd});
        EXPECT_EQ(part.raster.Transform(), expected.transform);
        EXPECT_EQ(part.raster.NoData().has_value(), full.NoData().has_value());
        EXPECT_EQ(part.geoKeys, whole.geoKeys);
        for (std::size_t row = 0; row < part.raster.Height(); ++row) {
            for (std::size_t col = 0; col < part.raster.Width(); ++col) {
                ASSERT_EQ(Bits(part.raster(col, row)),
                          Bits(full(window.colBegin + col, window.rowBegin + row)))
                    << col << "," << row;
            }
        }
    }
}

}  // namespace

TEST(ReadGeoTiffWindow, CropsEveryFixtureLayoutAcrossChunksAndAtTheEdges) {
    const std::vector<terraingen::PixelWindow> windows{
        {0, 21, 0, 19},  {0, 5, 0, 4},     {17, 21, 15, 19}, {3, 19, 2, 18},
        {14, 18, 1, 17}, {15, 16, 15, 17}, {20, 21, 0, 19},  {10, 40, 12, 99}};
    for (const GdalFixture* fixture : kGdalFixtures) {
        SCOPED_TRACE(fixture->name);
        const auto path =
            WriteBytes(std::string("window_") + fixture->name + ".tif", fixture->bytes);
        ExpectWindowsCropTheImage(path, fixture->band, windows);
    }
}

TEST(ReadGeoTiffWindow, CropsWrittenTilesAndDecodesOnlyThoseMeetingTheWindow) {
    const Raster<float> raster = Sample(50, 37);
    for (const TiffCompression compression :
         {TiffCompression::None, TiffCompression::Deflate, TiffCompression::Lzw}) {
        SCOPED_TRACE(static_cast<int>(compression));
        const auto path = TempPath("window_tiles.tif");
        GeoTiffWriteOptions options = Tiled(16);
        options.compression = compression;
        WriteGeoTiff(path, raster, {}, options);
        ExpectWindowsCropTheImage(path, 0,
                                  {{0, 50, 0, 37},
                                   {15, 33, 15, 33},
                                   {48, 50, 32, 37},
                                   {0, 1, 0, 1},
                                   {20, 30, 20, 30},
                                   {31, 90, 0, 16}});
        const std::size_t before = terraingen::detail::DecodedChunkCount();
        terraingen::ReadGeoTiffWindow(path, 0, {20, 30, 20, 30});
        EXPECT_EQ(terraingen::detail::DecodedChunkCount() - before, 1u);
        terraingen::ReadGeoTiffWindow(path, 0, {15, 17, 15, 17});
        EXPECT_EQ(terraingen::detail::DecodedChunkCount() - before, 5u);
        ReadGeoTiff(path);
        EXPECT_EQ(terraingen::detail::DecodedChunkCount() - before, 5u + 12u);
    }
}

TEST(ReadGeoTiffWindow, RejectsWindowsMissingTheImageAndMissingBands) {
    const auto path = TempPath("window_errors.tif");
    WriteGeoTiff(path, Sample(20, 10));
    EXPECT_THROW(terraingen::ReadGeoTiffWindow(path, 0, {5, 5, 0, 10}), std::invalid_argument);
    EXPECT_THROW(terraingen::ReadGeoTiffWindow(path, 0, {0, 20, 10, 12}), std::invalid_argument);
    EXPECT_THROW(terraingen::ReadGeoTiffWindow(path, 0, {25, 30, 0, 10}), std::invalid_argument);
    EXPECT_THROW(terraingen::ReadGeoTiffWindow(path, 1, {0, 20, 0, 10}), GeoTiffError);
}

namespace {

/// Checks that the blocks `GeoTiffBlockReader` reads of band `band` of `path` tile the whole
/// image read at once, transform and no-data value included.
void ExpectBlocksTileTheImage(const std::filesystem::path& path, std::size_t band) {
    const Raster<float> full = ReadGeoTiff(path, band).raster;
    const terraingen::GeoTiffBlockReader reader(path, band);
    EXPECT_EQ(reader.Info().grid, full.Grid());
    const std::size_t blockWidth = reader.BlockWidth();
    const std::size_t blockHeight = reader.BlockHeight();
    const std::size_t across = (full.Width() + blockWidth - 1) / blockWidth;
    const std::size_t down = (full.Height() + blockHeight - 1) / blockHeight;
    for (std::size_t by = 0; by < down; ++by) {
        for (std::size_t bx = 0; bx < across; ++bx) {
            SCOPED_TRACE(::testing::Message() << "block " << bx << "," << by);
            const Raster<float> block = reader.ReadBlock(bx, by);
            const std::size_t col0 = bx * blockWidth;
            const std::size_t row0 = by * blockHeight;
            ASSERT_EQ(block.Width(), std::min(blockWidth, full.Width() - col0));
            ASSERT_EQ(block.Height(), std::min(blockHeight, full.Height() - row0));
            const terraingen::RasterGrid expected = terraingen::Subgrid(
                full.Grid(), {col0, col0 + block.Width(), row0, row0 + block.Height()});
            EXPECT_EQ(block.Transform(), expected.transform);
            EXPECT_EQ(block.NoData().has_value(), full.NoData().has_value());
            for (std::size_t row = 0; row < block.Height(); ++row) {
                for (std::size_t col = 0; col < block.Width(); ++col) {
                    ASSERT_EQ(Bits(block(col, row)), Bits(full(col0 + col, row0 + row)))
                        << col << "," << row;
                }
            }
        }
    }
}

}  // namespace

TEST(GeoTiffBlockReader, TilesEveryFixtureLayoutWithItsBlocks) {
    for (const GdalFixture* fixture : kGdalFixtures) {
        SCOPED_TRACE(fixture->name);
        const auto path =
            WriteBytes(std::string("blocks_") + fixture->name + ".tif", fixture->bytes);
        ExpectBlocksTileTheImage(path, fixture->band);
    }
}

TEST(GeoTiffBlockReader, DecodesOneTilePerBlock) {
    const auto path = TempPath("blocks_tiles.tif");
    WriteGeoTiff(path, Sample(50, 37), {}, Tiled(16));
    ExpectBlocksTileTheImage(path, 0);
    const terraingen::GeoTiffBlockReader reader(path, 0);
    EXPECT_EQ(reader.BlockWidth(), 16u);
    EXPECT_EQ(reader.BlockHeight(), 16u);
    const std::size_t before = terraingen::detail::DecodedChunkCount();
    reader.ReadBlock(3, 2);
    EXPECT_EQ(terraingen::detail::DecodedChunkCount() - before, 1u);
}

TEST(GeoTiffBlockReader, GivesStripsSpanningEveryColumn) {
    const GdalFixture& fixture = float32_none_strips;
    const auto path = WriteBytes("blocks_strips.tif", fixture.bytes);
    const terraingen::GeoTiffBlockReader reader(path, 0);
    EXPECT_EQ(reader.BlockWidth(), fixture.width);
    EXPECT_FALSE(reader.Info().tiled);
}

TEST(GeoTiffBlockReader, ReadsBlocksFromSeveralThreadsAlike) {
    const auto path = TempPath("blocks_threads.tif");
    GeoTiffWriteOptions options = Tiled(16);
    options.compression = TiffCompression::Deflate;
    WriteGeoTiff(path, Sample(64, 64), {}, options);
    const terraingen::GeoTiffBlockReader reader(path, 0);
    std::vector<Raster<float>> expected;
    for (std::size_t block = 0; block < 16; ++block) {
        expected.push_back(reader.ReadBlock(block % 4, block / 4));
    }
    constexpr std::size_t kThreads = 4;
    std::vector<std::thread> threads;
    std::vector<int> mismatches(kThreads, 0);
    for (std::size_t t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t n = 0; n < 64; ++n) {
                const std::size_t block = (n * 7 + t * 3) % 16;
                const Raster<float> read = reader.ReadBlock(block % 4, block / 4);
                if (read.Values()[5] != expected[block].Values()[5] ||
                    read.Values()[200] != expected[block].Values()[200]) {
                    ++mismatches[t];
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(mismatches, std::vector<int>(kThreads, 0));
}

TEST(GeoTiffBlockReader, RejectsMissingBandsAndBlocksBeyondTheImage) {
    const auto path = TempPath("blocks_errors.tif");
    WriteGeoTiff(path, Sample(50, 37), {}, Tiled(16));
    EXPECT_THROW(terraingen::GeoTiffBlockReader(path, 1), GeoTiffError);
    const auto missing = TempPath("blocks_missing.tif");
    std::filesystem::remove(missing);
    EXPECT_THROW(terraingen::GeoTiffBlockReader(missing, 0), GeoTiffError);
    const terraingen::GeoTiffBlockReader reader(path, 0);
    EXPECT_THROW(reader.ReadBlock(4, 0), std::invalid_argument);
    EXPECT_THROW(reader.ReadBlock(0, 3), std::invalid_argument);
    EXPECT_NO_THROW(reader.ReadBlock(3, 2));
}

TEST(ParseGdalBandScaling, ReadsEachBandsScaleAndOffsetAndSkipsTheRest) {
    using terraingen::BandScaling;
    using terraingen::detail::ParseGdalBandScaling;
    EXPECT_EQ(ParseGdalBandScaling("", 2), (std::vector<BandScaling>{{}, {}}));
    const std::string xml = R"(<GDALMetadata>
  <Item name="OFFSET" sample="1" role="offset">-12.5</Item>
  <Item name="SCALE" sample="1" role="scale">0.25</Item>
  <Item name="SCALE" sample="0" role="scale">3</Item>
  <Item name="SCALE" sample="7" role="scale">9</Item>
  <Item name="SCALE" role="scale">9</Item>
  <Item name="SCALE" sample="x" role="scale">9</Item>
  <Item name="SCALE" sample="0" role="scale">nope</Item>
  <Item name="UNITTYPE" sample="0" role="unittype">m</Item>
  <Item name="AREA_OR_POINT">Area</Item>
  <Item name="SCALE" sample="0" role="scale">unterminated
</GDALMetadata>)";
    EXPECT_EQ(ParseGdalBandScaling(xml, 2), (std::vector<BandScaling>{{3.0, 0.0}, {0.25, -12.5}}));
    EXPECT_TRUE(BandScaling{}.Identity());
    EXPECT_FALSE((BandScaling{1.0, 2.0}).Identity());
}

TEST(ReadGeoTiffInfo, DescribesTheLayout) {
    const auto lzw = ReadGeoTiffInfo(WriteBytes("info_lzw.tif", float32_lzw_fp_tiles.bytes));
    EXPECT_EQ(lzw.compression, TiffCompression::Lzw);
    EXPECT_EQ(lzw.predictor, 3);
    EXPECT_TRUE(lzw.tiled);
    EXPECT_EQ(lzw.sampleFormat, terraingen::TiffSampleFormat::Float);
    EXPECT_EQ(lzw.bitsPerSample, 32);
    const auto big =
        ReadGeoTiffInfo(WriteBytes("info_big.tif", int32_lzw_horizontal_big_endian.bytes));
    EXPECT_TRUE(big.bigEndian);
    EXPECT_EQ(big.sampleFormat, terraingen::TiffSampleFormat::SignedInteger);
    EXPECT_TRUE(ReadGeoTiffInfo(WriteBytes("info_bigtiff.tif", float32_lzw_bigtiff.bytes)).bigTiff);
    EXPECT_EQ(ReadGeoTiffInfo(WriteBytes("info_pack.tif", uint8_packbits_strips.bytes)).compression,
              TiffCompression::PackBits);
    const auto point = ReadGeoTiffInfo(WriteBytes("info_point.tif", float32_pixel_is_point.bytes));
    EXPECT_TRUE(point.geoKeys.PixelIsPoint());
    EXPECT_FALSE(lzw.geoKeys.PixelIsPoint());
    EXPECT_FALSE(lzw.geoKeys.IsGeographic());
    EXPECT_EQ(terraingen::detail::ShortGeoKey(lzw.geoKeys, 1024), 1)
        << "GTModelTypeGeoKey: projected";
    EXPECT_EQ(terraingen::detail::ShortGeoKey(lzw.geoKeys, 4321), std::nullopt);
    EXPECT_FALSE(GeoKeys{}.IsGeographic());
    EXPECT_TRUE((GeoKeys{{1, 1, 0, 1, 1024, 0, 1, 2}, {}, {}}.IsGeographic()));
}

TEST(WriteGeoTiff, RoundTripsValuesTransformNoDataAndGeoKeys) {
    const auto source = ReadGeoTiff(WriteBytes("source.tif", float32_lzw_fp_tiles.bytes));
    for (const TiffCompression compression :
         {TiffCompression::Lzw, TiffCompression::Deflate, TiffCompression::None}) {
        for (const bool bigTiff : {false, true}) {
            Raster<float> raster = Sample(53, 37);
            raster.SetNoData(-9999.0f);
            raster(4, 5) = -9999.0f;
            const auto path = TempPath("roundtrip.tif");
            WriteGeoTiff(path, raster, source.geoKeys,
                         GeoTiffWriteOptions{compression, 16, bigTiff});
            const auto info = ReadGeoTiffInfo(path);
            EXPECT_EQ(info.compression, compression);
            EXPECT_EQ(info.bigTiff, bigTiff);
            EXPECT_TRUE(info.tiled);
            const auto read = ReadGeoTiff(path);
            ExpectSameRaster(read.raster, raster);
            EXPECT_EQ(read.raster.Transform(), raster.Transform());
            EXPECT_EQ(read.raster.NoData(), raster.NoData());
            EXPECT_EQ(read.geoKeys, source.geoKeys);
        }
    }
}

TEST(WriteGeoTiff, KeepsPixelIsPointAndRotatedTransforms) {
    const auto point = ReadGeoTiff(WriteBytes("point.tif", float32_pixel_is_point.bytes));
    const auto path = TempPath("point_out.tif");
    WriteGeoTiff(path, point.raster, point.geoKeys);
    const auto read = ReadGeoTiff(path);
    EXPECT_EQ(read.raster.Transform(), point.raster.Transform());
    EXPECT_TRUE(read.geoKeys.PixelIsPoint());

    const auto rotated = ReadGeoTiff(WriteBytes("rotated.tif", float32_rotated.bytes));
    EXPECT_NE(rotated.raster.Transform().rowRotation, 0.0);
    WriteGeoTiff(path, rotated.raster, rotated.geoKeys);
    EXPECT_EQ(ReadGeoTiff(path).raster.Transform(), rotated.raster.Transform());

    Raster<float> plain(20, 10, 1.5f);
    WriteGeoTiff(path, plain);
    const auto info = ReadGeoTiffInfo(path);
    EXPECT_FALSE(info.georeferenced);
    EXPECT_TRUE(info.geoKeys.Empty());
}

TEST(WriteGeoTiff, WritesSeveralBandsOneAfterAnother) {
    const GeoTransform transform = GeoTransform::NorthUp(100.0, 200.0, 2.0);
    std::vector<Raster<float>> bands{Sample(21, 19, transform), Sample(21, 19, transform),
                                     Sample(21, 19, transform)};
    for (std::size_t band = 0; band < bands.size(); ++band) {
        bands[band].SetNoData(-1.0f);
        for (float& value : bands[band].Values()) {
            value += 10.0f * static_cast<float>(band);
        }
    }
    const auto path = TempPath("bands.tif");
    WriteGeoTiff(path, bands, {}, Tiled(16));
    EXPECT_EQ(ReadGeoTiffInfo(path).bands, 3u);
    for (std::size_t band = 0; band < bands.size(); ++band) {
        const auto read = ReadGeoTiff(path, band);
        ExpectSameRaster(read.raster, bands[band]);
        EXPECT_EQ(read.raster.Transform(), bands[band].Transform());
        EXPECT_EQ(read.raster.NoData(), bands[band].NoData());
    }
    WriteGeoTiff(path, std::vector<Raster<float>>{bands[0]});
    EXPECT_EQ(ReadGeoTiffInfo(path).bands, 1u);
    ExpectSameRaster(ReadGeoTiff(path).raster, bands[0]);

    EXPECT_THROW(WriteGeoTiff(path, std::vector<Raster<float>>{}), GeoTiffError);
    bands[1] = Sample(20, 19);
    EXPECT_THROW(WriteGeoTiff(path, bands), GeoTiffError);
    bands[1] = Sample(21, 19, GeoTransform::NorthUp(0.0, 0.0, 1.0));
    EXPECT_THROW(WriteGeoTiff(path, bands), GeoTiffError);
}

TEST(GeographicSphereKeys, DescribeLongitudeAndLatitudeOnTheBody) {
    const auto keys = terraingen::GeographicSphereKeys(3389500.0);
    EXPECT_TRUE(keys.IsGeographic());
    EXPECT_FALSE(keys.PixelIsPoint());
    EXPECT_EQ(terraingen::detail::ShortGeoKey(keys, 2054), 9102);
    EXPECT_EQ(keys.doubleParams, (std::vector<double>{3389500.0, 3389500.0}));
    const Raster<float> raster = Sample(17, 9, GeoTransform::NorthUp(-10.0, 5.0, 0.25));
    const auto path = TempPath("sphere.tif");
    WriteGeoTiff(path, raster, keys);
    const auto read = ReadGeoTiff(path);
    EXPECT_EQ(read.geoKeys, keys);
    EXPECT_EQ(read.raster.Transform(), raster.Transform());
    EXPECT_THROW(terraingen::GeographicSphereKeys(0.0), std::invalid_argument);
}

TEST(WriteGeoTiff, RejectsInvalidRequests) {
    EXPECT_THROW(WriteGeoTiff(TempPath("empty.tif"), Raster<float>()), GeoTiffError);
    const Raster<float> raster = Sample(8, 8);
    EXPECT_THROW(WriteGeoTiff(TempPath("tile.tif"), raster, {}, Tiled(20)), GeoTiffError);
    EXPECT_THROW(WriteGeoTiff(TempPath("tile.tif"), raster, {}, Tiled(0)), GeoTiffError);
    EXPECT_THROW(WriteGeoTiff(TempPath("pack.tif"), raster, {}, {TiffCompression::PackBits}),
                 GeoTiffError);
    EXPECT_THROW(WriteGeoTiff(TempPath("missing/dir/x.tif"), raster), GeoTiffError);
}

TEST(WriteGeoTiff, NamesTheFileInEveryError) {
    const Raster<float> raster = Sample(8, 8);
    const auto message = [](const std::filesystem::path& path, const auto& write) {
        try {
            write(path);
        } catch (const GeoTiffError& error) {
            return std::string(error.what());
        }
        return std::string("no error");
    };
    const auto tile = TempPath("tile.tif");
    EXPECT_EQ(message(tile, [&](const auto& path) { WriteGeoTiff(path, raster, {}, Tiled(20)); }),
              tile.string() + ": the tile size must be a positive multiple of 16");
    const auto empty = TempPath("empty.tif");
    EXPECT_EQ(message(empty, [](const auto& path) { WriteGeoTiff(path, Raster<float>()); }),
              empty.string() + ": cannot write an empty raster");
    const auto missing = TempPath("missing/dir/x.tif");
    EXPECT_EQ(message(missing, [&](const auto& path) { WriteGeoTiff(path, raster); })
                  .rfind(missing.string() + ": ", 0),
              0u);
}

TEST(ReadGeoTiff, NamesTheFileInCodecErrors) {
    std::vector<std::uint8_t> corrupt = float32_lzw_fp_tiles.bytes;
    ASSERT_EQ(ReadGeoTiffInfo(WriteBytes("lzw.tif", corrupt)).compression, TiffCompression::Lzw);
    for (std::size_t i = corrupt.size() / 4; i < corrupt.size() * 3 / 4; ++i) {
        corrupt[i] = 0xff;
    }
    const std::string error = ReadError(corrupt);
    EXPECT_EQ(error.rfind(TempPath("patched.tif").string() + ": Corrupt LZW data", 0), 0u) << error;
}

TEST(ReadGeoTiff, ReportsUnreadableAndUnsupportedFiles) {
    EXPECT_THROW(ReadGeoTiff(TempPath("does_not_exist.tif")), GeoTiffError);
    EXPECT_THROW(ReadGeoTiff(WriteBytes("text.tif", {'h', 'e', 'l', 'l', 'o', 0, 0, 0})),
                 GeoTiffError);
    EXPECT_THROW(ReadGeoTiff(WriteBytes("short.tif", {'I', 'I'})), GeoTiffError);
    EXPECT_THROW(ReadGeoTiff(WriteBytes("version.tif", {'I', 'I', 7, 0, 8, 0, 0, 0})),
                 GeoTiffError);
    EXPECT_THROW(ReadGeoTiff(WriteBytes("band.tif", float32_lzw_fp_tiles.bytes), 1), GeoTiffError);

    std::vector<std::uint8_t> truncated = float32_none_strips.bytes;
    truncated.resize(truncated.size() / 2);
    EXPECT_THROW(ReadGeoTiff(WriteBytes("truncated.tif", truncated)), GeoTiffError);

    const auto& strips = float32_none_strips.bytes;
    const auto contains = [](const std::string& message, const std::string& text) {
        return message.find(text) != std::string::npos;
    };
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 258, 12)), "12-bit samples"))
        << ReadError(PatchTag(strips, 258, 12));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 339, 6)), "sample format"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 7)), "JPEG compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 50000)), "ZSTD compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 34887)), "LERC compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 34925)), "LZMA compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 50001)), "WebP compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 259, 99)), "code 99 compression"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 256, 0)), "implausible image size"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 256, 0, true)), "missing required tag 256"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 273, 0, true)), "missing strip offsets"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 258, 16)), "16-bit floats"));
    const auto& integers = int16_lzw_horizontal_nodata.bytes;
    EXPECT_TRUE(
        contains(ReadError(PatchTag(integers, 317, 3)), "floating-point predictor needs float"));
    EXPECT_TRUE(contains(ReadError(PatchTag(integers, 317, 9)), "unknown predictor 9"));
    EXPECT_TRUE(contains(ReadError(PatchTag(float32_lzw_fp_tiles.bytes, 325, 0, true)),
                         "missing tile offsets"));
    EXPECT_TRUE(contains(ReadError(PatchTag(float32_lzw_fp_tiles.bytes, 322, 0)),
                         "zero tile or strip size"));
    EXPECT_TRUE(contains(ReadError(PatchTag(strips, 278, 1)), "fewer tile or strip offsets"));

    const std::string zstd = ReadError(PatchTag(strips, 259, 50000));
    EXPECT_TRUE(contains(zstd, "ZSTD compression is not supported")) << zstd;
    EXPECT_TRUE(contains(zstd, "gdal_translate -co COMPRESS=DEFLATE")) << zstd;
    EXPECT_TRUE(
        contains(ReadError(PatchTag(float32_deflate_fp_tiles.bytes, 259, 32946)), "no error"));
}

TEST(TiffCodecs, LzwRoundTripsAcrossCodeWidthsAndClears) {
    std::vector<std::uint8_t> data;
    std::uint32_t state = 12345;
    for (std::size_t i = 0; i < 200000; ++i) {
        state = state * 1664525u + 1013904223u;
        data.push_back(i % 3 == 0 ? static_cast<std::uint8_t>(state >> 24)
                                  : static_cast<std::uint8_t>(i / 7));
    }
    for (const std::size_t size :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{600}, data.size()}) {
        const std::vector<std::uint8_t> input(data.begin(),
                                              data.begin() + static_cast<std::ptrdiff_t>(size));
        const auto encoded = terraingen::detail::EncodeLzw(input);
        EXPECT_EQ(terraingen::detail::DecodeLzw(encoded, size), input) << size;
    }
    const std::vector<std::uint8_t> zeros(70000, 0);
    const auto encoded = terraingen::detail::EncodeLzw(zeros);
    EXPECT_LT(encoded.size(), zeros.size() / 20);
    EXPECT_EQ(terraingen::detail::DecodeLzw(encoded, zeros.size()), zeros);
}

TEST(TiffCodecs, DeflateRoundTripsAndRejectsCorruptStreams) {
    std::vector<std::uint8_t> data(100000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 5));
    }
    const auto encoded = terraingen::detail::EncodeDeflate(data);
    EXPECT_LT(encoded.size(), data.size());
    EXPECT_EQ(encoded[0] & 0x0f, 8) << "zlib header with the Deflate method";
    EXPECT_EQ(terraingen::detail::DecodeDeflate(encoded, data.size()), data);
    EXPECT_THROW(terraingen::detail::DecodeDeflate(encoded, data.size() + 1), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodeDeflate(encoded, data.size() - 1), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodeDeflate(Bytes({0x78, 0x9c, 0xff, 0xff}), 8),
                 GeoTiffError);
    const std::vector<std::uint8_t> empty;
    EXPECT_EQ(terraingen::detail::DecodeDeflate(terraingen::detail::EncodeDeflate(empty), 0),
              empty);
}

TEST(TiffCodecs, RejectCorruptStreams) {
    const std::vector<std::uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    const auto encoded = terraingen::detail::EncodeLzw(data);
    EXPECT_THROW(terraingen::detail::DecodeLzw(encoded, data.size() + 1), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodeLzw(Bytes({0x00, 0x01, 0x02}), 4), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodeLzw(Bytes({0x80, 0x7f, 0xff}), 4), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodeLzw(Bytes({0xff, 0xff, 0xff}), 4), GeoTiffError);
    EXPECT_EQ(terraingen::detail::DecodePackBits(Bytes({0x02, 1, 2, 3, 0xfe, 9, 0x80}), 6),
              (std::vector<std::uint8_t>{1, 2, 3, 9, 9, 9}));
    EXPECT_THROW(terraingen::detail::DecodePackBits(Bytes({0x05, 1, 2}), 6), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodePackBits(Bytes({0xfe}), 3), GeoTiffError);
    EXPECT_THROW(terraingen::detail::DecodePackBits(Bytes({0x00, 1}), 3), GeoTiffError);
}
