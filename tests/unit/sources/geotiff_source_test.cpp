#include "terraingen/sources/geotiff_source.h"

#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/resample.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <stdexcept>

using terraingen::GeoKeys;
using terraingen::GeoTiffSource;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::ResampleMethod;

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

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

}  // namespace

TEST(GeoTiffSource, FillsItsGridWithTheFile) {
    const auto path = TempPath("geotiff_source_full.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path);
    EXPECT_EQ(source.Grid(), RasterGrid::Of(dem));
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

    RasterGrid shifted{2, 2, GeoTransform::NorthUp(1001.0, 2000.0, 4.0)};
    auto off = shifted.MakeRaster();
    EXPECT_THROW(source.Fill(off), std::invalid_argument);
    RasterGrid finer{2, 2, GeoTransform::NorthUp(1000.0, 2000.0, 2.0)};
    auto fine = finer.MakeRaster();
    EXPECT_THROW(source.Fill(fine), std::invalid_argument);
}

TEST(GeoTiffSource, ResamplesToThePixelSize) {
    const auto path = TempPath("geotiff_source_resampled.tif");
    const Raster<float> dem = WriteDem(path);
    const GeoTiffSource source(path, 0, 2.0, ResampleMethod::Bilinear);
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
    EXPECT_THROW(GeoTiffSource(path, 1), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(path, 0, -1.0), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(TempPath("geotiff_source_missing.tif")), terraingen::GeoTiffError);
    const auto degrees = TempPath("geotiff_source_degrees.tif");
    WriteDem(degrees, Keys(true));
    EXPECT_THROW(GeoTiffSource{degrees}, std::invalid_argument);
    Raster<float> rotated(4, 4, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    const auto turned = TempPath("geotiff_source_rotated.tif");
    terraingen::WriteGeoTiff(turned, rotated, Keys());
    EXPECT_NO_THROW(GeoTiffSource{turned});
    EXPECT_THROW(GeoTiffSource(turned, 0, 0.5), std::invalid_argument);
}
