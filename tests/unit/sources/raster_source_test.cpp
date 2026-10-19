#include "terraingen/sources/raster_source.h"

#include "support/temp_path.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/sources/geotiff_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::GeoKeys;
using terraingen::GeoTiffSource;
using terraingen::GeoTiffSourceOptions;
using terraingen::GeoTransform;
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

/// Checks that two sources fill `grid` and give heights at points over it to the same bits.
void ExpectSameHeights(const terraingen::Source& a, const terraingen::Source& b,
                       const RasterGrid& grid) {
    auto first = grid.MakeRaster();
    auto second = grid.MakeRaster();
    a.Fill(first);
    b.Fill(second);
    EXPECT_EQ(first.NoData().has_value(), second.NoData().has_value());
    for (std::size_t i = 0; i < first.Size(); ++i) {
        ASSERT_TRUE(SameBits(first.Values()[i], second.Values()[i])) << i;
    }
    const terraingen::Bounds2d box = grid.Bounds();
    for (double x = box.minX - 3.0; x < box.maxX + 3.0; x += 1.37) {
        for (double y = box.minY - 3.0; y < box.maxY + 3.0; y += 1.61) {
            ASSERT_TRUE(SameBits(a.HeightAt(x, y), b.HeightAt(x, y))) << x << "," << y;
            ASSERT_EQ(a.ResolutionAt(x, y), b.ResolutionAt(x, y)) << x << "," << y;
        }
    }
}

}  // namespace

TEST(RasterSource, BehavesAsAGeoTiffSourceOfTheSameRaster) {
    const Raster<float> dem = Dem();
    const auto path = TempPath("raster_source_dem.tif");
    terraingen::WriteGeoTiff(path, dem, Keys());
    std::vector<RasterSourceOptions> variants(4);
    variants[1].pixelSize = 1.5;
    variants[1].resampling = ResampleMethod::Bilinear;
    variants[2].lowPass = 6.0;
    variants[3].pixelSize = 3.0;
    variants[3].lowPass = 5.0;
    for (const RasterSourceOptions& options : variants) {
        GeoTiffSourceOptions read;
        read.pixelSize = options.pixelSize;
        read.resampling = options.resampling;
        read.lowPass = options.lowPass;
        const GeoTiffSource file(path, read);
        const RasterSource memory(dem, options, Keys());
        EXPECT_EQ(memory.Options(), options);
        EXPECT_EQ(memory.Grid(), file.Grid());
        EXPECT_EQ(memory.ReferencePoint().x, file.ReferencePoint().x);
        EXPECT_EQ(memory.ReferencePoint().y, file.ReferencePoint().y);
        EXPECT_EQ(memory.Keys(), Keys());
        ExpectSameHeights(memory, file, file.Grid());
        ExpectSameHeights(memory, file, {7, 5, GeoTransform::NorthUp(1081.0, 1971.0, 4.0)});
        ExpectSameHeights(memory, file, {9, 9, GeoTransform::NorthUp(990.0, 2010.0, 2.5)});
    }
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
    EXPECT_TRUE(std::isnan(source.HeightAt(hole.x, hole.y)));
    const auto cell = dem.CellCenter(5, 6);
    EXPECT_EQ(source.HeightAt(cell.x, cell.y), dem(5, 6));
    auto heights = source.Grid().MakeRaster();
    source.Fill(heights);
    EXPECT_EQ(heights.NoData(), -9999.0f);
    EXPECT_EQ(heights(3, 4), -9999.0f);
    EXPECT_TRUE(source.Keys().Empty());
}

TEST(RasterSource, RejectsUnusableRastersAndOptions) {
    EXPECT_THROW(RasterSource{Raster<float>{}}, std::invalid_argument);
    EXPECT_THROW(RasterSource{std::shared_ptr<const Raster<float>>{}}, std::invalid_argument);
    for (const double size : {0.0, -1.0, kInf, std::nan("")}) {
        RasterSourceOptions options;
        options.pixelSize = size;
        EXPECT_THROW(RasterSource(Dem(), options), std::invalid_argument) << size;
    }
    for (const double lowPass : {-1.0, kInf, std::nan("")}) {
        RasterSourceOptions options;
        options.lowPass = lowPass;
        EXPECT_THROW(RasterSource(Dem(), options), std::invalid_argument) << lowPass;
    }
    Raster<float> rotated(4, 4, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    EXPECT_NO_THROW(RasterSource{rotated});
    RasterSourceOptions resampled;
    resampled.pixelSize = 0.5;
    EXPECT_THROW(RasterSource(rotated, resampled), std::invalid_argument);
    const RasterSource turned(rotated);
    auto grid = RasterGrid{2, 2, GeoTransform{0.5, 1.0, 0.2, 0.0, 0.1, -1.0}}.MakeRaster();
    EXPECT_THROW(turned.Fill(grid), std::invalid_argument);
}

TEST(RasterSource, ResolvesItsNativeCellsCoarsenedByTheLowPass) {
    Raster<float> dem = Dem();
    dem.SetNoData(-1.0f);
    dem(2, 2) = -1.0f;
    const auto centre = dem.CellCenter(10, 10);
    const auto hole = dem.CellCenter(2, 2);
    const RasterSource native(dem);
    EXPECT_EQ(native.ResolutionAt(centre.x, centre.y), 4.0);
    EXPECT_EQ(native.ResolutionAt(hole.x, hole.y), kInf);
    EXPECT_EQ(native.ResolutionAt(900.0, 2000.0), kInf);
    RasterSourceOptions fine;
    fine.pixelSize = 0.5;
    EXPECT_EQ(RasterSource(dem, fine).ResolutionAt(centre.x, centre.y), 4.0)
        << "resampling resolves nothing finer";
    RasterSourceOptions smooth;
    smooth.lowPass = 3.0;
    EXPECT_EQ(RasterSource(dem, smooth).ResolutionAt(centre.x, centre.y), 6.0);
    smooth.lowPass = 1.0;
    EXPECT_EQ(RasterSource(dem, smooth).ResolutionAt(centre.x, centre.y), 4.0);
    Raster<float> oblong(8, 8, 1.0f);
    oblong.SetTransform(GeoTransform{0.0, 2.0, 0.0, 0.0, 0.0, -5.0});
    EXPECT_EQ(RasterSource(oblong).ResolutionAt(3.0, -7.0), 5.0);
}
