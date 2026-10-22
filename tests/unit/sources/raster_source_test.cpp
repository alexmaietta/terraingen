#include "terraingen/sources/raster_source.h"

#include "support/temp_path.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::DiamondSquareOptions;
using terraingen::DiamondSquareUpsampler;
using terraingen::GeoKeys;
using terraingen::GeoTiffSource;
using terraingen::GeoTiffSourceOptions;
using terraingen::GeoTransform;
using terraingen::KernelUpsampler;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::RasterSource;
using terraingen::RasterSourceOptions;
using terraingen::ResampleMethod;
using terraingen::testing::TempPath;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

GeoKeys Keys() {
    return {{1, 1, 0, 1, 1024, 0, 1, 1}, {}, {}};
}

/// A 24 x 18 DEM of uneven heights in 4 m cells with its origin at (1000, 2000).
Raster<float> Dem() {
    Raster<float> dem(24, 18);
    dem.SetTransform(GeoTransform::NorthUp(1000.0, 2000.0, 4.0));
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(3.0 * std::sin(0.6 * static_cast<double>(col)) +
                                               0.25 * static_cast<double>(row * row));
        }
    }
    return dem;
}

bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

/// Diamond-square with its roughness given.
std::shared_ptr<const DiamondSquareUpsampler> DiamondSquare() {
    DiamondSquareOptions options;
    options.fractalDimension = 2.3;
    options.scale = 0.4;
    options.seed = 3;
    return std::make_shared<DiamondSquareUpsampler>(options);
}

/// Checks that two sources fill `grid` and give heights at points over it to the same bits, at
/// `spacing`.
void ExpectSameHeights(const terraingen::Source& a, const terraingen::Source& b,
                       const RasterGrid& grid, double spacing) {
    auto first = grid.MakeRaster();
    auto second = grid.MakeRaster();
    a.Fill(first, spacing);
    b.Fill(second, spacing);
    EXPECT_EQ(first.NoData().has_value(), second.NoData().has_value());
    for (std::size_t i = 0; i < first.Size(); ++i) {
        ASSERT_TRUE(SameBits(first.Values()[i], second.Values()[i])) << i;
    }
    const terraingen::Bounds2d box = grid.Bounds();
    for (double x = box.minX - 3.0; x < box.maxX + 3.0; x += 1.37) {
        for (double y = box.minY - 3.0; y < box.maxY + 3.0; y += 1.61) {
            ASSERT_TRUE(SameBits(a.HeightAt(x, y, spacing), b.HeightAt(x, y, spacing)))
                << x << "," << y;
            ASSERT_EQ(a.ResolutionAt(x, y), b.ResolutionAt(x, y)) << x << "," << y;
        }
    }
}

/// Checks that points, tiles and the whole of `grid` at its own spacing agree to the bit.
void ExpectPointsTilesAndGridAgree(const terraingen::Source& source, const RasterGrid& grid) {
    const double spacing = grid.PixelSize();
    Raster<float> whole = grid.MakeRaster();
    source.Fill(whole, spacing);
    const terraingen::PixelWindow tiles[] = {
        {0, 7, 0, 5}, {7, grid.width, 0, 5}, {0, grid.width, 5, grid.height}};
    for (const terraingen::PixelWindow& cells : tiles) {
        Raster<float> tile = terraingen::Subgrid(grid, cells).MakeRaster();
        source.Fill(tile, spacing);
        for (std::size_t row = 0; row < tile.Height(); ++row) {
            for (std::size_t col = 0; col < tile.Width(); ++col) {
                ASSERT_TRUE(
                    SameBits(tile(col, row), whole(cells.colBegin + col, cells.rowBegin + row)));
            }
        }
    }
    for (std::size_t row = 0; row < whole.Height(); ++row) {
        for (std::size_t col = 0; col < whole.Width(); ++col) {
            const terraingen::Vec2d center = whole.CellCenter(col, row);
            const float point = source.HeightAt(center.x, center.y, spacing);
            const float cell = whole(col, row);
            ASSERT_TRUE(SameBits(point, cell) || (std::isnan(point) && whole.IsNoData(cell)))
                << col << "," << row;
        }
    }
}

}  // namespace

TEST(RasterSource, BehavesAsAGeoTiffSourceOfTheSameRaster) {
    const Raster<float> dem = Dem();
    const auto path = TempPath("raster_source_dem.tif");
    terraingen::WriteGeoTiff(path, dem, Keys());
    std::vector<RasterSourceOptions> variants(4);
    variants[1].upsampler = std::make_shared<KernelUpsampler>(ResampleMethod::Bilinear);
    variants[2].effectivePixelSize = 6.0;
    variants[3].upsampler = DiamondSquare();
    for (const RasterSourceOptions& options : variants) {
        GeoTiffSourceOptions read;
        read.effectivePixelSize = options.effectivePixelSize;
        read.upsampler = options.upsampler;
        const GeoTiffSource file(path, read);
        const RasterSource memory(dem, options, Keys());
        EXPECT_EQ(memory.Options(), options);
        EXPECT_EQ(memory.Grid(), file.Grid());
        EXPECT_EQ(memory.ReferencePoint().x, file.ReferencePoint().x);
        EXPECT_EQ(memory.ReferencePoint().y, file.ReferencePoint().y);
        EXPECT_EQ(memory.Keys(), Keys());
        for (const double spacing : {4.0, 1.5, 0.3}) {
            ExpectSameHeights(memory, file, file.Grid(), spacing);
            ExpectSameHeights(memory, file, {7, 5, GeoTransform::NorthUp(1081.0, 1971.0, 4.0)},
                              spacing);
            ExpectSameHeights(memory, file, {9, 9, GeoTransform::NorthUp(990.0, 2010.0, 2.5)},
                              spacing);
        }
    }
}

TEST(RasterSource, CubicByDefaultGivesTodaysResamplingToTheBit) {
    const Raster<float> dem = Dem();
    const RasterSource source(dem);
    for (const double pixelSize : {1.3, 0.5, 2.0}) {
        const Raster<float> resampled = terraingen::Resample(dem, pixelSize, ResampleMethod::Cubic);
        Raster<float> filled = RasterGrid::FromRaster(resampled).MakeRaster();
        source.Fill(filled, pixelSize);
        for (std::size_t i = 0; i < filled.Size(); ++i) {
            ASSERT_TRUE(SameBits(filled.Values()[i], resampled.Values()[i]))
                << pixelSize << " " << i;
        }
        for (std::size_t row = 0; row < filled.Height(); row += 3) {
            for (std::size_t col = 0; col < filled.Width(); col += 5) {
                const terraingen::Vec2d center = filled.CellCenter(col, row);
                ASSERT_TRUE(SameBits(
                    source.HeightAt(center.x, center.y, pixelSize),
                    terraingen::SampleRaster(dem, center.x, center.y, ResampleMethod::Cubic)));
            }
        }
    }
}

TEST(RasterSource, PointsTilesAndGridsAgreeForEveryUpsampler) {
    const std::vector<std::shared_ptr<const terraingen::Upsampler>> upsamplers{
        nullptr, std::make_shared<KernelUpsampler>(ResampleMethod::Nearest),
        std::make_shared<KernelUpsampler>(ResampleMethod::Lanczos3),
        std::make_shared<KernelUpsampler>(ResampleMethod::CubicBSpline), DiamondSquare()};
    for (const auto& upsampler : upsamplers) {
        RasterSourceOptions options;
        options.upsampler = upsampler;
        Raster<float> dem = Dem();
        dem.SetNoData(-9999.0f);
        dem(6, 6) = -9999.0f;
        const RasterSource source(dem, options);
        ExpectPointsTilesAndGridAgree(source, {23, 17, GeoTransform::NorthUp(1003.1, 1996.3, 0.7)});
        ExpectPointsTilesAndGridAgree(source, {30, 20, GeoTransform::NorthUp(1000.0, 2000.0, 2.0)});
        ExpectPointsTilesAndGridAgree(source, {20, 14, GeoTransform::NorthUp(990.0, 2010.0, 6.0)});
    }
}

TEST(RasterSource, CopiesItsCellsOnItsLatticeAtItsSpacingOrCoarser) {
    const Raster<float> dem = Dem();
    RasterSourceOptions smooth;
    smooth.upsampler = std::make_shared<KernelUpsampler>(ResampleMethod::CubicBSpline);
    const RasterSource source(dem, smooth);
    for (const double spacing : {4.0, 10.0}) {
        Raster<float> heights = source.Grid().MakeRaster();
        source.Fill(heights, spacing);
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            ASSERT_TRUE(SameBits(heights.Values()[i], dem.Values()[i])) << i;
        }
    }
    const terraingen::Vec2d cell = dem.CellCenter(5, 6);
    EXPECT_EQ(source.HeightAt(cell.x, cell.y, 4.0), dem(5, 6));
    EXPECT_NE(source.HeightAt(cell.x, cell.y, 1.0), dem(5, 6))
        << "the B-spline smooths the cells at finer spacings";
    EXPECT_EQ(RasterSource(dem).HeightAt(cell.x, cell.y, 1.0), dem(5, 6))
        << "cubic interpolation passes through the cells";
}

TEST(RasterSource, SharesTheRasterItIsGivenAndHonoursNoData) {
    Raster<float> dem = Dem();
    dem.SetNoData(-9999.0f);
    dem(3, 4) = -9999.0f;
    const auto shared = std::make_shared<const Raster<float>>(dem);
    const RasterSource source(shared);
    EXPECT_GT(shared.use_count(), 1) << "the raster is not copied";
    EXPECT_EQ(source.Grid(), RasterGrid::FromRaster(dem));
    const auto hole = dem.CellCenter(3, 4);
    EXPECT_TRUE(std::isnan(source.HeightAt(hole.x, hole.y, 4.0)));
    EXPECT_TRUE(std::isnan(source.HeightAt(hole.x + 0.3, hole.y, 1.0)));
    const auto cell = dem.CellCenter(5, 6);
    EXPECT_EQ(source.HeightAt(cell.x, cell.y, 4.0), dem(5, 6));
    auto heights = source.Grid().MakeRaster();
    source.Fill(heights);
    EXPECT_EQ(heights.NoData(), -9999.0f);
    EXPECT_EQ(heights(3, 4), -9999.0f);
    auto fine = terraingen::ResampledGrid(source.Grid(), 2.0).MakeRaster();
    source.Fill(fine);
    EXPECT_EQ(fine.NoData(), -9999.0f);
    EXPECT_EQ(fine(6, 8), -9999.0f);
    EXPECT_TRUE(source.Keys().Empty());
}

TEST(RasterSource, RejectsUnusableRastersAndOptions) {
    EXPECT_THROW(RasterSource{Raster<float>{}}, std::invalid_argument);
    EXPECT_THROW(RasterSource{std::shared_ptr<const Raster<float>>{}}, std::invalid_argument);
    for (const double size : {0.0, -1.0, kInf, std::nan("")}) {
        RasterSourceOptions options;
        options.effectivePixelSize = size;
        EXPECT_THROW(RasterSource(Dem(), options), std::invalid_argument) << size;
    }
    Raster<float> rotated(4, 4, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    EXPECT_NO_THROW(RasterSource{rotated});
    const RasterSource turned(rotated);
    auto grid = RasterGrid{2, 2, GeoTransform{0.5, 1.0, 0.2, 0.0, 0.1, -1.0}}.MakeRaster();
    EXPECT_THROW(turned.Fill(grid), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(turned.HeightAt(0.7, -1.3, 0.1)), std::invalid_argument);
}

TEST(RasterSource, ResolvesItsCellsOrItsEffectivePixelSize) {
    Raster<float> dem = Dem();
    dem.SetNoData(-1.0f);
    dem(2, 2) = -1.0f;
    const auto center = dem.CellCenter(10, 10);
    const auto hole = dem.CellCenter(2, 2);
    const RasterSource native(dem);
    EXPECT_EQ(native.ResolutionAt(center.x, center.y), 4.0);
    EXPECT_EQ(native.ResolutionAt(hole.x, hole.y), kInf);
    EXPECT_EQ(native.ResolutionAt(900.0, 2000.0), kInf);
    RasterSourceOptions upsampled;
    upsampled.effectivePixelSize = 20.0;
    const RasterSource coarse(dem, upsampled);
    EXPECT_EQ(coarse.ResolutionAt(center.x, center.y), 20.0);
    EXPECT_EQ(coarse.ResolutionAt(hole.x, hole.y), kInf);
    Raster<float> oblong(8, 8, 1.0f);
    oblong.SetTransform(GeoTransform{0.0, 2.0, 0.0, 0.0, 0.0, -5.0});
    EXPECT_EQ(RasterSource(oblong).ResolutionAt(3.0, -7.0), 5.0);
}
