// SPDX-License-Identifier: BSD-3-Clause

#include "support/raster_equality.h"
#include "support/source_agreement.h"
#include "terraingen/sources/raster_source.h"

#include "sources/raster_heights.h"
#include "support/resample.h"
#include "support/temp_path.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"

#include <gtest/gtest.h>

#include <atomic>
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
using terraingen::testing::ExpectPointsTilesAndGridAgree;
using terraingen::testing::SameBits;
using terraingen::testing::TempPath;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

GeoKeys Keys() {
    return {{1, 1, 0, 1, 1024, 0, 1, 1}, {}, {}};
}

/// A 24 x 18 DEM of uneven heights in 4 m cells with its origin at (1000, 2000).
Raster<float> Dem() {
    Raster<float> dem(RasterGrid{24, 18, GeoTransform::NorthUp(1000.0, 2000.0, 4.0)});
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(3.0 * std::sin(0.6 * static_cast<double>(col)) +
                                               0.25 * static_cast<double>(row * row));
        }
    }
    return dem;
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
    Raster<float> first(grid);
    Raster<float> second(grid);
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
        const Raster<float> resampled =
            terraingen::testing::Resample(dem, pixelSize, ResampleMethod::Cubic);
        Raster<float> filled(resampled.Grid());
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
        Raster<float> heights(source.Grid());
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

TEST(RasterSource, HonoursNoData) {
    Raster<float> dem = Dem();
    dem.SetNoData(-9999.0f);
    dem(3, 4) = -9999.0f;
    const RasterSource source(dem);
    EXPECT_EQ(source.Grid(), dem.Grid());
    const auto hole = dem.CellCenter(3, 4);
    EXPECT_TRUE(std::isnan(source.HeightAt(hole.x, hole.y, 4.0)));
    EXPECT_TRUE(std::isnan(source.HeightAt(hole.x + 0.3, hole.y, 1.0)));
    const auto cell = dem.CellCenter(5, 6);
    EXPECT_EQ(source.HeightAt(cell.x, cell.y, 4.0), dem(5, 6));
    Raster<float> heights(source.Grid());
    source.Fill(heights);
    EXPECT_EQ(heights.NoData(), -9999.0f);
    EXPECT_EQ(heights(3, 4), -9999.0f);
    Raster<float> fine(terraingen::testing::ResampledGrid(source.Grid(), 2.0));
    source.Fill(fine);
    EXPECT_EQ(fine.NoData(), -9999.0f);
    EXPECT_EQ(fine(6, 8), -9999.0f);
    EXPECT_TRUE(source.Keys().Empty());
}

TEST(RasterSource, RejectsUnusableRastersAndOptions) {
    EXPECT_THROW(RasterSource{Raster<float>{}}, std::invalid_argument);
    for (const double size : {0.0, -1.0, kInf, std::nan("")}) {
        RasterSourceOptions options;
        options.effectivePixelSize = size;
        EXPECT_THROW(RasterSource(Dem(), options), std::invalid_argument) << size;
    }
    Raster<float> rotated(RasterGrid{4, 4, GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0}}, 1.0f);
    EXPECT_NO_THROW(RasterSource{rotated});
    const RasterSource turned(rotated);
    Raster<float> grid(RasterGrid{2, 2, GeoTransform{0.5, 1.0, 0.2, 0.0, 0.1, -1.0}});
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
    const auto beside = dem.CellCenter(3, 2);
    EXPECT_EQ(native.ResolutionAt(beside.x + 1.0, beside.y - 1.5), 4.0)
        << "the cell under the point holds data, though cubic taps reach the hole";
    EXPECT_EQ(native.ResolutionAt(hole.x + 1.5, hole.y + 1.0), kInf) << "the hole's own cell";
    EXPECT_EQ(native.ResolutionAt(900.0, 2000.0), kInf);
    RasterSourceOptions upsampled;
    upsampled.effectivePixelSize = 20.0;
    const RasterSource coarse(dem, upsampled);
    EXPECT_EQ(coarse.ResolutionAt(center.x, center.y), 20.0);
    EXPECT_EQ(coarse.ResolutionAt(hole.x, hole.y), kInf);
    EXPECT_EQ(coarse.PostSpacingAt(center.x, center.y), 4.0) << "the posts stay the cells";
    EXPECT_EQ(coarse.PostSpacingAt(hole.x, hole.y), kInf);
    Raster<float> oblong(RasterGrid{8, 8, GeoTransform{0.0, 2.0, 0.0, 0.0, 0.0, -5.0}}, 1.0f);
    EXPECT_EQ(RasterSource(oblong).ResolutionAt(3.0, -7.0), 5.0);
}

namespace {

/// A 24 x 12 globe of uneven heights in 15 degree cells, its west edge at longitude `west`.
Raster<float> Globe(double west, std::size_t shift = 0) {
    Raster<float> globe(RasterGrid{24, 12, GeoTransform::NorthUp(west, 90.0, 15.0)});
    for (std::size_t row = 0; row < globe.Height(); ++row) {
        for (std::size_t col = 0; col < globe.Width(); ++col) {
            const double at = static_cast<double>((col + shift) % globe.Width());
            globe(col, row) = static_cast<float>(40.0 * std::sin(0.7 * at) + 3.0 * at +
                                                 5.0 * static_cast<double>(row));
        }
    }
    return globe;
}

}  // namespace

TEST(RasterSource, WrapsAGlobeInLongitude) {
    const GeoKeys degrees = terraingen::GeographicSphereKeys(1737400.0);
    const Raster<float> globe = Globe(0.0);
    const RasterSource source(globe, {}, degrees);
    EXPECT_EQ(source.HeightAt(-172.5, 82.5, 15.0), globe(12, 0));
    EXPECT_EQ(source.HeightAt(547.5, 82.5, 15.0), globe(12, 0));
    EXPECT_EQ(source.HeightAt(367.5, -82.5, 15.0), globe(0, 11));
    EXPECT_EQ(source.ResolutionAt(-172.5, 0.0), 15.0);

    Raster<float> east(RasterGrid{24, 12, GeoTransform::NorthUp(-180.0, 90.0, 15.0)});
    source.Fill(east, 15.0);
    for (std::size_t row = 0; row < east.Height(); ++row) {
        for (std::size_t col = 0; col < east.Width(); ++col) {
            ASSERT_EQ(east(col, row), globe((col + 12) % 24, row)) << col << ", " << row;
        }
    }
    EXPECT_FALSE(east.NoData().has_value());

    const RasterSource centered(Globe(-180.0, 12), {}, degrees);
    Raster<float> seam(RasterGrid{36, 6, GeoTransform::NorthUp(-45.0, 45.0, 2.5)});
    Raster<float> across = seam;
    source.Fill(seam, 2.5);
    centered.Fill(across, 2.5);
    for (std::size_t row = 0; row < seam.Height(); ++row) {
        for (std::size_t col = 0; col < seam.Width(); ++col) {
            const terraingen::Vec2d center = seam.CellCenter(col, row);
            ASSERT_NEAR(seam(col, row), across(col, row), 1e-3) << col << ", " << row;
            ASSERT_TRUE(SameBits(seam(col, row), source.HeightAt(center.x, center.y, 2.5)))
                << col << ", " << row;
        }
    }
}

TEST(RasterSource, WrapsOnlyGeographicRastersSpanningATurn) {
    const GeoKeys degrees = terraingen::GeographicSphereKeys(1737400.0);
    EXPECT_TRUE(terraingen::detail::WrapsLongitude(degrees, Globe(0.0).Grid()));
    EXPECT_TRUE(std::isnan(RasterSource(Globe(0.0), {}, Keys()).HeightAt(-172.5, 0.0, 15.0)));
    EXPECT_TRUE(std::isnan(RasterSource(Globe(0.0)).HeightAt(-172.5, 0.0, 15.0)));
    Raster<float> partial(RasterGrid{23, 12, GeoTransform::NorthUp(0.0, 90.0, 15.0)}, 1.0f);
    EXPECT_FALSE(terraingen::detail::WrapsLongitude(degrees, partial.Grid()));
    EXPECT_TRUE(std::isnan(RasterSource(partial, {}, degrees).HeightAt(-7.5, 0.0, 15.0)));
}

TEST(RasterSource, CopiesItsLatticeInStripsOnEveryThreadCount) {
    Raster<float> heights(RasterGrid{150, 200, GeoTransform::NorthUp(0.0, 400.0, 2.0)}, 0.0f);
    heights.SetNoData(-9999.0f);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            heights(col, row) = static_cast<float>(col) + 1000.0f * static_cast<float>(row);
        }
    }
    heights(7, 130) = -9999.0f;
    const RasterSource source(heights);
    Raster<float> serial(RasterGrid{140, 210, GeoTransform::NorthUp(10.0, 410.0, 2.0)});
    Raster<float> parallel(RasterGrid{140, 210, serial.Transform()});
    source.Fill(serial, 2.0, 1);
    source.Fill(parallel, 2.0, 0);
    terraingen::testing::ExpectSameRaster(serial, parallel);
    EXPECT_EQ(serial(0, 5), 5.0f + 0.0f) << "lattice column 5, row 0";
    EXPECT_EQ(serial(100, 150), 105.0f + 145000.0f);
    EXPECT_TRUE(serial.IsNoData(serial(2, 135))) << "the no-data post";
    EXPECT_TRUE(serial.IsNoData(serial(0, 0))) << "above the lattice";
    EXPECT_TRUE(serial.IsNoData(serial(139, 209))) << "beyond the lattice";
}

namespace {

/// Cubic upsampling that counts the samples it upsamples.
class CountingUpsampler final : public terraingen::Upsampler {
  public:
    mutable std::atomic<std::size_t> samples{0};

  protected:
    void FillSamples(const terraingen::PostLattice& posts, terraingen::SampleBlock& block,
                     double spacing, unsigned threads) const override {
        samples += block.Size();
        cubic_.Fill(posts, block, spacing, threads);
    }

  private:
    KernelUpsampler cubic_;
};

}  // namespace

TEST(RasterSource, UpsamplesOnlyTheCellsItCoversToTheSameBits) {
    auto counting = std::make_shared<CountingUpsampler>();
    RasterSourceOptions options;
    options.upsampler = counting;
    const RasterSource source(Dem(), options);
    const RasterGrid wide{200, 200, GeoTransform::NorthUp(900.0, 2100.0, 1.0)};
    Raster<float> heights(wide);
    source.Fill(heights, 1.0);
    EXPECT_LE(counting->samples.load(), std::size_t{98 * 74}) << "the 96 x 72 cells covered";
    std::size_t covered = 0;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            const float point = source.HeightAt(center.x, center.y, 1.0);
            ASSERT_TRUE(SameBits(heights(col, row), point) ||
                        (heights.IsNoData(heights(col, row)) && std::isnan(point)))
                << col << "," << row;
            covered += std::isnan(point) ? 0 : 1;
        }
    }
    EXPECT_EQ(covered, std::size_t{96 * 72});
    const RasterGrid away{20, 20, GeoTransform::NorthUp(0.0, 0.0, 1.0)};
    Raster<float> nothing(away);
    counting->samples = 0;
    source.Fill(nothing, 1.0);
    EXPECT_EQ(counting->samples.load(), 0u) << "a grid off the raster upsamples nothing";
    EXPECT_TRUE(nothing.IsNoData(nothing(3, 3)));
}
