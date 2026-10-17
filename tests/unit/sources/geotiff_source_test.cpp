#include "terraingen/sources/geotiff_source.h"

#include "support/temp_path.h"
#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "tests/unit/raster/gdal_fixtures.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using terraingen::GeoKeys;
using terraingen::GeoTiffSource;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::ResampleMethod;
using terraingen::testing::TempPath;

namespace {

GeoKeys Keys(bool geographic = false) {
    return {{1, 1, 0, 1, 1024, 0, 1, static_cast<std::uint16_t>(geographic ? 2 : 1)}, {}, {}};
}

/// Writes a 20 x 10 DEM of 4 m cells with its origin at (1000, 2000) and returns it.
Raster<float> WriteDem(const std::filesystem::path& path, const GeoKeys& keys = Keys()) {
    Raster<float> dem(20, 10);
    dem.SetTransform(GeoTransform::NorthUp(1000.0, 2000.0, 4.0));
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(col) + 100.0f * static_cast<float>(row);
        }
    }
    terraingen::WriteGeoTiff(path, dem, keys);
    return dem;
}

}  // namespace

TEST(GeoTiffSource, FillsItsGridWithTheFile) {
    const auto path = TempPath("geotiff_source_full.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path);
    EXPECT_EQ(source.Grid(), RasterGrid::FromRaster(dem));
    EXPECT_EQ(source.ReferencePoint().x, 1000.0);
    EXPECT_EQ(source.ReferencePoint().y, 2000.0);
    EXPECT_EQ(source.Keys(), Keys());
    auto heights = source.Grid().MakeRaster();
    source.Fill(heights);
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], dem.Values()[i]) << i;
    }
    EXPECT_FALSE(heights.NoData().has_value());
}

TEST(GeoTiffSource, FillsTilesOnItsLatticeAndNoDataBeyondTheFile) {
    const auto path = TempPath("geotiff_source_tile.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path);
    RasterGrid tile{4, 3, GeoTransform::NorthUp(1000.0 + 4.0 * 18, 2000.0 - 4.0 * 8, 4.0)};
    auto heights = tile.MakeRaster();
    source.Fill(heights);
    EXPECT_EQ(heights(0, 0), dem(18, 8));
    EXPECT_EQ(heights(1, 1), dem(19, 9));
    EXPECT_TRUE(std::isnan(heights(2, 0)));
    EXPECT_TRUE(std::isnan(heights(0, 2)));
    ASSERT_TRUE(heights.NoData().has_value());
    EXPECT_TRUE(heights.IsNoData(heights(3, 2)));

    RasterGrid rotated{2, 2, GeoTransform{1000.0, 4.0, 0.5, 2000.0, 0.5, -4.0}};
    auto turned = rotated.MakeRaster();
    EXPECT_THROW(source.Fill(turned), std::invalid_argument);
}

TEST(GeoTiffSource, InterpolatesOntoGridsOffItsLattice) {
    const auto path = TempPath("geotiff_source_off.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path, {.resampling = ResampleMethod::Bilinear});
    for (const RasterGrid& grid : {RasterGrid{6, 4, GeoTransform::NorthUp(1001.0, 1999.0, 4.0)},
                                   RasterGrid{8, 6, GeoTransform::NorthUp(1000.0, 2000.0, 2.0)},
                                   RasterGrid{5, 5, GeoTransform::NorthUp(990.0, 2010.0, 8.0)}}) {
        auto heights = grid.MakeRaster();
        source.Fill(heights);
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                const auto centre = heights.CellCenter(col, row);
                const float expected =
                    terraingen::SampleRaster(dem, centre.x, centre.y, ResampleMethod::Bilinear);
                if (std::isnan(expected)) {
                    EXPECT_TRUE(heights.IsNoData(heights(col, row)));
                } else {
                    EXPECT_EQ(heights(col, row), expected) << col << "," << row;
                }
                EXPECT_EQ(std::isnan(source.HeightAt(centre.x, centre.y)), std::isnan(expected));
            }
        }
    }
}

TEST(GeoTiffSource, ResamplesToThePixelSize) {
    const auto path = TempPath("geotiff_source_resampled.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path, {.pixelSize = 2.0, .resampling = ResampleMethod::Bilinear});
    EXPECT_EQ(source.Grid().width, 40u);
    EXPECT_EQ(source.Grid().height, 20u);
    auto heights = source.Grid().MakeRaster();
    source.Fill(heights);
    const Raster<float> expected = terraingen::Resample(dem, 2.0, ResampleMethod::Bilinear);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(GeoTiffSource, RejectsUnusableFiles) {
    const auto path = TempPath("geotiff_source_errors.tif");
    WriteDem(path);
    EXPECT_THROW(GeoTiffSource(path, {.band = 1}), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(path, {.pixelSize = -1.0}), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(TempPath("geotiff_source_missing.tif")), terraingen::GeoTiffError);
    const auto degrees = TempPath("geotiff_source_degrees.tif");
    WriteDem(degrees, Keys(true));
    EXPECT_THROW(GeoTiffSource{degrees}, std::invalid_argument);
    Raster<float> rotated(4, 4, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    const auto turned = TempPath("geotiff_source_rotated.tif");
    terraingen::WriteGeoTiff(turned, rotated, Keys());
    EXPECT_NO_THROW(GeoTiffSource{turned});
    EXPECT_THROW(GeoTiffSource(turned, {.pixelSize = 0.5}), std::invalid_argument);
}

TEST(GeoTiffSource, GivesCellsAtTheirCentresAndInterpolatesElsewhere) {
    const auto path = TempPath("geotiff_source_point.tif");
    Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path);
    EXPECT_EQ(source.HeightAt(1000.0 + 4.0 * 3 + 2.0, 2000.0 - 4.0 * 5 - 2.0), dem(3, 5));
    EXPECT_EQ(source.HeightAt(1000.0 + 4.0 * 3 + 3.0, 2000.0 - 4.0 * 5 - 1.0),
              terraingen::SampleRaster(dem, 1015.0, 1979.0, ResampleMethod::Cubic));
    EXPECT_TRUE(std::isnan(source.HeightAt(999.0, 1990.0)));
    EXPECT_TRUE(std::isnan(source.HeightAt(1100.0, 2001.0)));
    dem.SetNoData(-9999.0f);
    dem(1, 1) = -9999.0f;
    const auto holes = TempPath("geotiff_source_holes.tif");
    terraingen::WriteGeoTiff(holes, dem, Keys());
    EXPECT_TRUE(std::isnan(GeoTiffSource(holes).HeightAt(1000.0 + 6.0, 2000.0 - 6.0)));
}

TEST(GeoTiffSource, ScalesSamplesByTheBandsMetadataOrOverrides) {
    using terraingen::testing::GdalFixture;
    const auto write = [](const GdalFixture& fixture) {
        const auto path = TempPath(std::string("scaled_") + fixture.name + ".tif");
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(fixture.bytes.data()),
                   static_cast<std::streamsize>(fixture.bytes.size()));
        return path;
    };
    const GdalFixture& single = terraingen::testing::int16_scaled_nodata;
    const auto expect = [&](const GeoTiffSource& source, const GdalFixture& fixture, double scale,
                            double offset) {
        EXPECT_EQ(source.Scaling(), (terraingen::BandScaling{scale, offset}));
        auto heights = source.Grid().MakeRaster();
        source.Fill(heights);
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            const float raw = fixture.values[i];
            if (fixture.noData && raw == static_cast<float>(*fixture.noData)) {
                ASSERT_TRUE(std::isnan(heights.Values()[i])) << i;
                continue;
            }
            ASSERT_EQ(heights.Values()[i],
                      static_cast<float>(static_cast<double>(raw) * scale + offset))
                << i;
        }
    };
    const auto path = write(single);
    expect(GeoTiffSource(path), single, 0.5, -1000.0);
    expect(GeoTiffSource(path, {.scale = 2.0}), single, 2.0, -1000.0);
    expect(GeoTiffSource(path, {.offset = 0.0}), single, 0.5, 0.0);
    auto heights = GeoTiffSource(path).Grid().MakeRaster();
    GeoTiffSource(path).Fill(heights);
    ASSERT_TRUE(heights.NoData().has_value());
    EXPECT_TRUE(std::isnan(*heights.NoData()));
    EXPECT_TRUE(std::isnan(
        GeoTiffSource(path).HeightAt(-1200.0 + 4 * 2.5 + 1.25, 3400.0 - 3 * 2.5 - 1.25)));

    const GdalFixture& bands = terraingen::testing::uint16_three_band_scaled;
    expect(GeoTiffSource(write(bands), {.band = 1}), bands, 0.25, 10.0);
    EXPECT_EQ(GeoTiffSource(write(bands)).Scaling(), terraingen::BandScaling{});

    EXPECT_THROW(GeoTiffSource(path, {.scale = std::nan("")}), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(path, {.scale = 1.0, .offset = INFINITY}), std::invalid_argument);
}

TEST(GeoTiffSource, LowPassesTheFileByAGaussianInMetres) {
    const auto path = TempPath("geotiff_source_low_pass.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource smooth(path, {.lowPass = 8.0});
    const Raster<float> expected = terraingen::GaussianBlur(dem, 2.0);
    auto heights = smooth.Grid().MakeRaster();
    smooth.Fill(heights);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
    const auto centre = dem.CellCenter(5, 4);
    EXPECT_EQ(smooth.HeightAt(centre.x, centre.y), expected(5, 4));
    EXPECT_NE(smooth.HeightAt(centre.x, centre.y), dem(5, 4)) << "detail is removed";
    EXPECT_THROW(GeoTiffSource(path, {.lowPass = -1.0}), std::invalid_argument);
}
