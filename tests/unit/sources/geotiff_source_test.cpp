#include "terraingen/sources/geotiff_source.h"

#include "support/temp_path.h"
#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/geotiff/tiff_format.h"
#include "terraingen/raster/resample.h"
#include "tests/unit/raster/gdal_fixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

using terraingen::GeoKeys;
using terraingen::GeoTiffSource;
using terraingen::GeoTiffSourceOptions;
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
    GeoTiffSourceOptions bilinear;
    bilinear.resampling = ResampleMethod::Bilinear;
    const GeoTiffSource source(path, bilinear);
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
    GeoTiffSourceOptions options;
    options.pixelSize = 2.0;
    options.resampling = ResampleMethod::Bilinear;
    const GeoTiffSource source(path, options);
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
    GeoTiffSourceOptions secondBand;
    secondBand.band = 1;
    EXPECT_THROW(GeoTiffSource(path, secondBand), std::invalid_argument);
    GeoTiffSourceOptions negative;
    negative.pixelSize = -1.0;
    EXPECT_THROW(GeoTiffSource(path, negative), std::invalid_argument);
    EXPECT_THROW(GeoTiffSource(TempPath("geotiff_source_missing.tif")), terraingen::GeoTiffError);
    const auto degrees = TempPath("geotiff_source_degrees.tif");
    WriteDem(degrees, Keys(true));
    EXPECT_THROW(GeoTiffSource{degrees}, std::invalid_argument);
    Raster<float> rotated(4, 4, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    const auto turned = TempPath("geotiff_source_rotated.tif");
    terraingen::WriteGeoTiff(turned, rotated, Keys());
    EXPECT_NO_THROW(GeoTiffSource{turned});
    GeoTiffSourceOptions resampled;
    resampled.pixelSize = 0.5;
    EXPECT_THROW(GeoTiffSource(turned, resampled), std::invalid_argument);
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
    GeoTiffSourceOptions scaled;
    scaled.scale = 2.0;
    expect(GeoTiffSource(path, scaled), single, 2.0, -1000.0);
    GeoTiffSourceOptions unshifted;
    unshifted.offset = 0.0;
    expect(GeoTiffSource(path, unshifted), single, 0.5, 0.0);
    auto heights = GeoTiffSource(path).Grid().MakeRaster();
    GeoTiffSource(path).Fill(heights);
    ASSERT_TRUE(heights.NoData().has_value());
    EXPECT_TRUE(std::isnan(*heights.NoData()));
    EXPECT_TRUE(std::isnan(
        GeoTiffSource(path).HeightAt(-1200.0 + 4 * 2.5 + 1.25, 3400.0 - 3 * 2.5 - 1.25)));

    const GdalFixture& bands = terraingen::testing::uint16_three_band_scaled;
    GeoTiffSourceOptions secondBand;
    secondBand.band = 1;
    expect(GeoTiffSource(write(bands), secondBand), bands, 0.25, 10.0);
    EXPECT_EQ(GeoTiffSource(write(bands)).Scaling(), terraingen::BandScaling{});

    GeoTiffSourceOptions notANumber;
    notANumber.scale = std::nan("");
    EXPECT_THROW(GeoTiffSource(path, notANumber), std::invalid_argument);
    GeoTiffSourceOptions infinite;
    infinite.scale = 1.0;
    infinite.offset = INFINITY;
    EXPECT_THROW(GeoTiffSource(path, infinite), std::invalid_argument);
}

TEST(GeoTiffSource, LowPassesTheFileByAGaussianInMetres) {
    const auto path = TempPath("geotiff_source_low_pass.tif");
    const Raster<float> dem = WriteDem(path);
    GeoTiffSourceOptions options;
    options.lowPass = 8.0;
    const GeoTiffSource smooth(path, options);
    const Raster<float> expected = terraingen::GaussianBlur(dem, 2.0);
    auto heights = smooth.Grid().MakeRaster();
    smooth.Fill(heights);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
    const auto centre = dem.CellCenter(5, 4);
    EXPECT_EQ(smooth.HeightAt(centre.x, centre.y), expected(5, 4));
    EXPECT_NE(smooth.HeightAt(centre.x, centre.y), dem(5, 4)) << "detail is removed";
    options.lowPass = -1.0;
    EXPECT_THROW(GeoTiffSource(path, options), std::invalid_argument);
}

namespace {

/// Writes a 60 x 45 DEM of uneven heights in 4 m cells, with its origin at (1000, 2000), a
/// no-data hole and 16-pixel tiles.
void WriteLargeDem(const std::filesystem::path& path) {
    Raster<float> dem(60, 45);
    dem.SetTransform(GeoTransform::NorthUp(1000.0, 2000.0, 4.0));
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(
                5.0 * std::sin(0.31 * static_cast<double>(col)) +
                2.0 * std::cos(0.47 * static_cast<double>(row) + 0.1 * static_cast<double>(col)) +
                0.05 * static_cast<double>(row * col));
        }
    }
    dem.SetNoData(-9999.0f);
    dem(30, 20) = -9999.0f;
    terraingen::GeoTiffWriteOptions tiles;
    tiles.tileSize = 16;
    terraingen::WriteGeoTiff(path, dem, Keys(), tiles);
}

bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

TEST(GeoTiffSource, ARegionGivesTheSameBitsOverItsGridAndNoDataBeyond) {
    const auto path = TempPath("geotiff_source_region.tif");
    WriteLargeDem(path);
    const std::vector<terraingen::Bounds2d> regions{{1081.0, 1883.0, 1149.0, 1931.0},
                                                    {950.0, 1951.0, 1062.0, 2100.0},
                                                    {1201.0, 1700.0, 1300.0, 1851.0}};
    for (const double lowPass : {0.0, 7.0}) {
        for (const std::optional<double> pixelSize :
             {std::optional<double>(), std::optional(1.5)}) {
            for (const ResampleMethod method : {ResampleMethod::Cubic, ResampleMethod::Bilinear}) {
                GeoTiffSourceOptions options;
                options.lowPass = lowPass;
                options.pixelSize = pixelSize;
                options.resampling = method;
                const GeoTiffSource full(path, options);
                for (const terraingen::Bounds2d& region : regions) {
                    SCOPED_TRACE(::testing::Message()
                                 << "lowPass " << lowPass << " pixelSize " << pixelSize.value_or(0)
                                 << " method " << static_cast<int>(method) << " region "
                                 << region.minX << "," << region.minY);
                    GeoTiffSourceOptions clipped = options;
                    clipped.region = region;
                    const std::size_t before = terraingen::detail::DecodedChunkCount();
                    const GeoTiffSource part(path, clipped);
                    const RasterGrid grid = part.Grid();
                    EXPECT_EQ(grid, terraingen::Subgrid(full.Grid(), terraingen::PixelWindowIn(
                                                                         full.Grid(), region)));
                    EXPECT_EQ(part.ReferencePoint().x, full.ReferencePoint().x);
                    EXPECT_EQ(part.ReferencePoint().y, full.ReferencePoint().y);
                    auto mine = grid.MakeRaster();
                    auto theirs = grid.MakeRaster();
                    part.Fill(mine);
                    EXPECT_LT(terraingen::detail::DecodedChunkCount() - before, 12u)
                        << "only a window of the 4 x 3 tiles is read";
                    full.Fill(theirs);
                    EXPECT_EQ(mine.NoData(), theirs.NoData());
                    for (std::size_t i = 0; i < mine.Size(); ++i) {
                        ASSERT_TRUE(SameBits(mine.Values()[i], theirs.Values()[i])) << i;
                    }
                    const terraingen::Bounds2d box = grid.Bounds();
                    for (double x = box.minX + 0.01; x < box.maxX; x += 0.93) {
                        for (double y = box.minY + 0.01; y < box.maxY; y += 1.07) {
                            ASSERT_TRUE(SameBits(part.HeightAt(x, y), full.HeightAt(x, y)))
                                << x << "," << y;
                            ASSERT_EQ(part.ResolutionAt(x, y), full.ResolutionAt(x, y));
                        }
                    }
                    for (std::size_t row = 0; row < grid.height; ++row) {
                        for (std::size_t col = 0; col < grid.width; ++col) {
                            const auto centre = mine.CellCenter(col, row);
                            ASSERT_TRUE(SameBits(part.HeightAt(centre.x, centre.y),
                                                 full.HeightAt(centre.x, centre.y)));
                        }
                    }
                    const double side = std::min(box.maxX - box.minX, box.maxY - box.minY) / 6.0;
                    const RasterGrid offLattice{
                        5, 4, GeoTransform::NorthUp(box.minX + 0.7, box.maxY - 0.3, side)};
                    auto offMine = offLattice.MakeRaster();
                    auto offTheirs = offLattice.MakeRaster();
                    part.Fill(offMine);
                    full.Fill(offTheirs);
                    for (std::size_t i = 0; i < offMine.Size(); ++i) {
                        ASSERT_TRUE(SameBits(offMine.Values()[i], offTheirs.Values()[i])) << i;
                    }
                    auto everything = full.Grid().MakeRaster();
                    part.Fill(everything);
                    ASSERT_TRUE(everything.NoData().has_value());
                    const terraingen::PixelWindow inside =
                        terraingen::PixelWindowIn(full.Grid(), region);
                    std::size_t outside = 0;
                    for (std::size_t row = 0; row < everything.Height(); ++row) {
                        for (std::size_t col = 0; col < everything.Width(); ++col) {
                            if (col >= inside.colBegin && col < inside.colEnd &&
                                row >= inside.rowBegin && row < inside.rowEnd) {
                                continue;
                            }
                            ++outside;
                            ASSERT_TRUE(everything.IsNoData(everything(col, row)));
                        }
                    }
                    EXPECT_GT(outside, 0u);
                    const double far = box.minX > 1100.0 ? 1010.0 : 1230.0;
                    EXPECT_TRUE(std::isnan(part.HeightAt(far, 1830.5)));
                    EXPECT_FALSE(std::isnan(full.HeightAt(far, 1830.5)));
                    EXPECT_TRUE(std::isinf(part.ResolutionAt(far, 1830.5)));
                }
            }
        }
    }
}

TEST(GeoTiffSource, RejectsRegionsThatAreNotFiniteEmptyOrMissTheFile) {
    const auto path = TempPath("geotiff_source_bad_region.tif");
    WriteDem(path);
    const double inf = std::numeric_limits<double>::infinity();
    for (const terraingen::Bounds2d& region :
         {terraingen::Bounds2d{1000.0, 1980.0, inf, 2000.0},
          terraingen::Bounds2d{std::nan(""), 1980.0, 1010.0, 2000.0},
          terraingen::Bounds2d{1010.0, 1980.0, 1010.0, 2000.0},
          terraingen::Bounds2d{1020.0, 1980.0, 1010.0, 2000.0},
          terraingen::Bounds2d{1000.0, 1990.0, 1010.0, 1980.0},
          terraingen::Bounds2d{2000.0, 1980.0, 2010.0, 2000.0},
          terraingen::Bounds2d{1000.0, 2000.0, 1010.0, 2010.0}}) {
        GeoTiffSourceOptions options;
        options.region = region;
        EXPECT_THROW(GeoTiffSource(path, options), std::invalid_argument)
            << region.minX << "," << region.minY << "," << region.maxX << "," << region.maxY;
    }
    GeoTiffSourceOptions corner;
    corner.region = terraingen::Bounds2d{1000.0, 1999.0, 1001.0, 2000.0};
    EXPECT_EQ(GeoTiffSource(path, corner).Grid().width, 1u);
    GeoTiffSourceOptions same = corner;
    EXPECT_EQ(same, corner);
    same.region->maxX = 1002.0;
    EXPECT_NE(same, corner);
}

TEST(GeoTiffSource, ResolvesTheFilesCellsCoarsenedByTheLowPass) {
    const auto path = TempPath("geotiff_source_resolution.tif");
    WriteDem(path);
    GeoTiffSourceOptions options;
    options.pixelSize = 1.0;
    EXPECT_EQ(GeoTiffSource(path, options).ResolutionAt(1010.0, 1990.0), 4.0);
    EXPECT_TRUE(std::isinf(GeoTiffSource(path, options).ResolutionAt(990.0, 1990.0)));
    options.lowPass = 8.0;
    EXPECT_EQ(GeoTiffSource(path, options).ResolutionAt(1010.0, 1990.0), 16.0);
}
