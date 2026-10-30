// SPDX-License-Identifier: BSD-3-Clause

#include "support/raster_equality.h"
#include "terraingen/sources/upsampling.h"

#include "terraingen/raster/resample.h"
#include "terraingen/sources/raster_source.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using terraingen::GeoTransform;
using terraingen::KernelUpsampler;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::RasterPlacement;
using terraingen::RasterPosts;
using terraingen::ResampleMethod;
using terraingen::testing::SameBits;

namespace {

/// A 12 x 9 raster of uneven heights in 3 m cells.
Raster<float> Heights() {
    Raster<float> raster(12, 9);
    raster.SetTransform(GeoTransform::NorthUp(-6.0, 30.0, 3.0));
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = static_cast<float>(std::cos(0.7 * static_cast<double>(col)) +
                                                  0.3 * static_cast<double>(row * col));
        }
    }
    return raster;
}

RasterPosts Posts(const Raster<float>& raster) {
    const auto shared = std::make_shared<const Raster<float>>(raster);
    return RasterPosts(shared, RasterPlacement::Whole(*shared));
}

/// Heights rising by 1 per post: col + 100 row.
class CountingPosts final : public terraingen::PostLattice {
  public:
    GeoTransform Transform() const override {
        return GeoTransform::NorthUp(0.0, 0.0, 2.0);
    }
    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              terraingen::Span<float> heights) const override {
        for (std::size_t r = 0; r < height; ++r) {
            for (std::size_t c = 0; c < width; ++c) {
                heights[r * width + c] = static_cast<float>(
                    static_cast<double>(col0 + static_cast<std::int64_t>(c)) +
                    100.0 * static_cast<double>(row0 + static_cast<std::int64_t>(r)));
            }
        }
    }
};

/// The mean of the four posts around a point, whatever the spacing.
class MeanUpsampler final : public terraingen::Upsampler {
  public:
    /// The mean of the four posts of `posts` around (`x`, `y`).
    static float Mean(const terraingen::PostLattice& posts, double x, double y) {
        const terraingen::Vec2d pixel = posts.Transform().WorldToPixel(x, y);
        std::vector<float> values(4);
        posts.Read(static_cast<std::int64_t>(std::floor(pixel.x - 0.5)),
                   static_cast<std::int64_t>(std::floor(pixel.y - 0.5)), 2, 2,
                   terraingen::Span<float>(values));
        return 0.25f * (values[0] + values[1] + values[2] + values[3]);
    }

  protected:
    void FillSamples(const terraingen::PostLattice& posts, terraingen::SampleBlock& samples,
                     double /*spacing*/, unsigned threads) const override {
        terraingen::detail::FillByTiles(
            samples, threads,
            [&](terraingen::Span<const terraingen::Vec2d> points, terraingen::Span<float> heights) {
                for (std::size_t i = 0; i < points.size(); ++i) {
                    heights[i] = Mean(posts, points[i].x, points[i].y);
                }
            });
    }
};

}  // namespace

TEST(RasterPosts, ReadsTheWindowRepeatingTheEdgesAndNoDataAsNan) {
    Raster<float> raster = Heights();
    raster.SetNoData(-1.0f);
    raster(2, 3) = -1.0f;
    const RasterPosts posts = Posts(raster);
    EXPECT_EQ(posts.Transform(), raster.Transform());
    ASSERT_TRUE(posts.Extent().has_value());
    EXPECT_EQ(*posts.Extent(), (terraingen::PixelWindow{0, 12, 0, 9}));
    std::vector<float> values(4 * 3);
    posts.Read(-2, 2, 4, 3, terraingen::Span<float>(values));
    EXPECT_EQ(values[0], raster(0, 2));
    EXPECT_EQ(values[1], raster(0, 2));
    EXPECT_EQ(values[2], raster(0, 2));
    EXPECT_EQ(values[3], raster(1, 2));
    EXPECT_EQ(values[8], raster(0, 4));
    std::vector<float> hole(1);
    posts.Read(2, 3, 1, 1, terraingen::Span<float>(hole));
    EXPECT_TRUE(std::isnan(hole[0]));
    std::vector<float> corner(1);
    posts.Read(20, 20, 1, 1, terraingen::Span<float>(corner));
    EXPECT_EQ(corner[0], raster(11, 8));

    const auto window = std::make_shared<const Raster<float>>(3, 2, 5.0f);
    const RasterPosts placed(window, {RasterGrid::FromRaster(raster), 4, 6});
    EXPECT_EQ(*placed.Extent(), (terraingen::PixelWindow{4, 7, 6, 8}));
    EXPECT_THROW(RasterPosts(nullptr, {}), std::invalid_argument);
}

TEST(RasterPosts, WrappingColumnsRepeatWithTheLatticesWidth) {
    const Raster<float> raster = Heights();
    const auto shared = std::make_shared<const Raster<float>>(raster);
    const RasterPosts posts(shared, RasterPlacement::Whole(raster), true);
    std::vector<float> values(4);
    posts.Read(-2, 3, 4, 1, terraingen::Span<float>(values));
    EXPECT_EQ(values[0], raster(10, 3));
    EXPECT_EQ(values[1], raster(11, 3));
    EXPECT_EQ(values[2], raster(0, 3));
    EXPECT_EQ(values[3], raster(1, 3));
    posts.Read(23, 3, 2, 1, terraingen::Span<float>(values).subspan(0, 2));
    EXPECT_EQ(values[0], raster(11, 3));
    EXPECT_EQ(values[1], raster(0, 3));
    posts.Read(0, -1, 1, 1, terraingen::Span<float>(values).subspan(0, 1));
    EXPECT_EQ(values[0], raster(0, 0)) << "rows still repeat their edge";

    const auto window = std::make_shared<const Raster<float>>(3, 2, 5.0f);
    EXPECT_THROW(RasterPosts(window, {RasterGrid::FromRaster(raster), 4, 6}, true),
                 std::invalid_argument);
}

TEST(KernelUpsampler, GivesSampleRastersBitsForEveryKernel) {
    const Raster<float> raster = Heights();
    const RasterPosts posts = Posts(raster);
    for (const ResampleMethod method :
         {ResampleMethod::Nearest, ResampleMethod::Bilinear, ResampleMethod::Cubic,
          ResampleMethod::Lanczos3, ResampleMethod::CubicBSpline}) {
        const KernelUpsampler upsampler(method);
        EXPECT_EQ(upsampler.Reach(), terraingen::KernelRadius(method));
        for (double x = -3.0; x < 28.0; x += 1.37) {
            for (double y = 4.0; y < 29.0; y += 1.13) {
                ASSERT_TRUE(SameBits(upsampler.HeightAt(posts, x, y, 0.5),
                                     terraingen::SampleRaster(raster, x, y, method)))
                    << static_cast<int>(method) << " " << x << "," << y;
            }
        }
        const RasterGrid grid{40, 30, GeoTransform::NorthUp(-5.3, 29.1, 0.77)};
        Raster<float> filled = grid.MakeRaster();
        upsampler.Fill(posts, filled, 0.77, 0);
        for (std::size_t row = 0; row < filled.Height(); ++row) {
            for (std::size_t col = 0; col < filled.Width(); ++col) {
                const terraingen::Vec2d center = filled.CellCenter(col, row);
                ASSERT_TRUE(SameBits(filled(col, row),
                                     upsampler.HeightAt(posts, center.x, center.y, 0.77)));
            }
        }
    }
}

TEST(KernelUpsampler, ReadsCellByCellWhenTheGridIsMuchCoarserThanThePosts) {
    const CountingPosts posts;
    const KernelUpsampler cubic;
    const RasterGrid coarse{6, 5, GeoTransform::NorthUp(-1000.0, 1000.0, 400.0)};
    Raster<float> filled = coarse.MakeRaster();
    cubic.Fill(posts, filled, 400.0, 0);
    for (std::size_t row = 0; row < filled.Height(); ++row) {
        for (std::size_t col = 0; col < filled.Width(); ++col) {
            const terraingen::Vec2d center = filled.CellCenter(col, row);
            EXPECT_TRUE(SameBits(filled(col, row), cubic.HeightAt(posts, center.x, center.y, 1.0)));
        }
    }
    EXPECT_NEAR(cubic.HeightAt(posts, 5.0, -3.0, 1.0), 2.0 + 100.0 * 1.0, 1e-3);
}

TEST(KernelUpsampler, RejectsARotatedLattice) {
    Raster<float> raster(4, 4, 1.0f);
    raster.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    const RasterPosts posts = Posts(raster);
    EXPECT_THROW(static_cast<void>(KernelUpsampler{}.HeightAt(posts, 1.0, -1.0, 0.1)),
                 std::invalid_argument);
}

TEST(Upsampler, GivesThePostsOwnHeightAtTheirSpacingOrCoarser) {
    const CountingPosts posts;
    const MeanUpsampler mean;
    EXPECT_TRUE(terraingen::AtPostSpacing(posts.Transform(), 2.0));
    EXPECT_TRUE(terraingen::AtPostSpacing(posts.Transform(), 2.0 * (1.0 - 1e-12)));
    EXPECT_FALSE(terraingen::AtPostSpacing(posts.Transform(), 1.9));
    EXPECT_EQ(mean.HeightAt(posts, 3.0, -5.0, 2.0), 1.0f + 200.0f);
    EXPECT_EQ(mean.HeightAt(posts, 3.0, -5.0, 1.0), MeanUpsampler::Mean(posts, 3.0, -5.0));
    EXPECT_EQ(mean.HeightAt(posts, 3.5, -5.0, 2.0), MeanUpsampler::Mean(posts, 3.5, -5.0));
    const RasterGrid grid{5, 4, GeoTransform::NorthUp(0.0, 0.0, 2.0)};
    for (const double spacing : {2.0, 0.5}) {
        Raster<float> filled = grid.MakeRaster();
        mean.Fill(posts, filled, spacing, 0);
        for (std::size_t row = 0; row < filled.Height(); ++row) {
            for (std::size_t col = 0; col < filled.Width(); ++col) {
                const terraingen::Vec2d center = filled.CellCenter(col, row);
                EXPECT_EQ(filled(col, row), mean.HeightAt(posts, center.x, center.y, spacing));
            }
        }
    }
    const GeoTransform oblong{0.0, 2.0, 0.0, 0.0, 0.0, -8.0};
    EXPECT_DOUBLE_EQ(terraingen::PostSpacing(oblong), 8.0);
    EXPECT_TRUE(terraingen::AtPostSpacing(oblong, RasterGrid{3, 3, oblong}.PixelSize()))
        << "a raster filled on its own grid copies its posts";
}

namespace {

/// `MeanUpsampler`, noting each thread that reads it.
class ThreadNotingUpsampler final : public terraingen::Upsampler {
  public:
    std::set<std::thread::id> Threads() const {
        const std::lock_guard lock(mutex_);
        return threads_;
    }

  protected:
    void FillSamples(const terraingen::PostLattice& posts, terraingen::SampleBlock& samples,
                     double /*spacing*/, unsigned threads) const override {
        terraingen::detail::FillByTiles(
            samples, threads,
            [&](terraingen::Span<const terraingen::Vec2d> points, terraingen::Span<float> heights) {
                {
                    const std::lock_guard lock(mutex_);
                    threads_.insert(std::this_thread::get_id());
                }
                for (std::size_t i = 0; i < points.size(); ++i) {
                    heights[i] = MeanUpsampler::Mean(posts, points[i].x, points[i].y);
                }
            });
    }

  private:
    mutable std::mutex mutex_;
    mutable std::set<std::thread::id> threads_;
};

}  // namespace

TEST(Upsampler, FillsOnTheThreadsGivenToTheSameBits) {
    const CountingPosts posts;
    const RasterGrid grid{40, 30, GeoTransform::NorthUp(1.0, -1.0, 0.5)};
    ThreadNotingUpsampler serial;
    Raster<float> one = grid.MakeRaster();
    serial.Fill(posts, one, 0.5, 1);
    EXPECT_EQ(serial.Threads(), std::set<std::thread::id>{std::this_thread::get_id()});
    ThreadNotingUpsampler parallel;
    Raster<float> every = grid.MakeRaster();
    parallel.Fill(posts, every, 0.5, 0);
    terraingen::testing::ExpectSameRaster(one, every);
    const Raster<float> heights = Heights();
    const RasterPosts lattice = Posts(heights);
    const RasterGrid fine{60, 40, GeoTransform::NorthUp(-6.0, 30.0, 0.6)};
    for (const ResampleMethod method : {ResampleMethod::Cubic, ResampleMethod::Lanczos3}) {
        const KernelUpsampler kernel(method);
        Raster<float> a = fine.MakeRaster();
        Raster<float> b = fine.MakeRaster();
        kernel.Fill(lattice, a, 0.6, 1);
        kernel.Fill(lattice, b, 0.6, 0);
        terraingen::testing::ExpectSameRaster(a, b);
    }
}

TEST(KernelUpsampler, FillsARotatedGridCellByCellToTheBitsOfItsPoints) {
    const Raster<float> heights = Heights();
    const RasterPosts lattice = Posts(heights);
    Raster<float> rotated(20, 14);
    rotated.SetTransform(GeoTransform{2.0, 0.5, 0.2, 20.0, 0.15, -0.5});
    for (const ResampleMethod method : {ResampleMethod::Cubic, ResampleMethod::Lanczos3}) {
        const KernelUpsampler kernel(method);
        kernel.Fill(lattice, rotated, 0.5, 0);
        for (std::size_t row = 0; row < rotated.Height(); ++row) {
            for (std::size_t col = 0; col < rotated.Width(); ++col) {
                const terraingen::Vec2d center = rotated.CellCenter(col, row);
                ASSERT_TRUE(
                    SameBits(rotated(col, row), kernel.HeightAt(lattice, center.x, center.y, 0.5)))
                    << col << "," << row;
            }
        }
    }
}

TEST(Upsampler, FillsAGridToTheBitsOfItsPointsAndFitsNothing) {
    const CountingPosts posts;
    const MeanUpsampler mean;
    EXPECT_EQ(mean.Reach(), 2);
    EXPECT_EQ(mean.FitTo(posts), nullptr);
    Raster<float> filled = RasterGrid{3, 3, GeoTransform::NorthUp(1.0, -1.0, 1.0)}.MakeRaster();
    mean.Fill(posts, filled, 1.0, 0);
    EXPECT_EQ(filled(1, 1), mean.HeightAt(posts, 2.5, -2.5, 1.0));
}

TEST(RegisterUpsampler, MakesRegisteredTypesByNameWithTheirSettings) {
    std::string received;
    terraingen::RegisterUpsampler("test_mean", [&received](const std::string& settings) {
        received = settings;
        return std::make_shared<MeanUpsampler>();
    });
    EXPECT_TRUE(terraingen::IsUpsamplerRegistered("test_mean"));
    EXPECT_FALSE(terraingen::IsUpsamplerRegistered("test_unknown"));
    const auto names = terraingen::RegisteredUpsamplerNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "test_mean"), names.end());
    const auto made = terraingen::MakeRegisteredUpsampler("test_mean", "{\"a\":1}");
    EXPECT_NE(dynamic_cast<const MeanUpsampler*>(made.get()), nullptr);
    EXPECT_EQ(received, "{\"a\":1}");
    EXPECT_THROW(terraingen::MakeRegisteredUpsampler("test_unknown"), std::invalid_argument);
    terraingen::RegisterUpsampler("test_null", [](const std::string&) { return nullptr; });
    EXPECT_THROW(terraingen::MakeRegisteredUpsampler("test_null"), std::invalid_argument);
    EXPECT_THROW(terraingen::RegisterUpsampler("", [](const std::string&) { return nullptr; }),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::RegisterUpsampler("cubic", [](const std::string&) { return nullptr; }),
                 std::invalid_argument);
    EXPECT_THROW(
        terraingen::RegisterUpsampler("diamond_square", [](const std::string&) { return nullptr; }),
        std::invalid_argument);
    EXPECT_THROW(terraingen::RegisterUpsampler("test_empty", {}), std::invalid_argument);
}

TEST(KernelUpsamplerNames, NameEveryKernel) {
    const std::vector<std::string> expected{"nearest",     "bilinear", "cubic",
                                            "catmull_rom", "lanczos3", "cubic_bspline"};
    EXPECT_EQ(terraingen::KernelUpsamplerNames(), expected);
    EXPECT_EQ(terraingen::KernelUpsamplerMethod("lanczos3"), ResampleMethod::Lanczos3);
    EXPECT_FALSE(terraingen::KernelUpsamplerMethod("diamond_square").has_value());
    const auto cubic =
        std::dynamic_pointer_cast<const KernelUpsampler>(terraingen::DefaultUpsampler());
    ASSERT_NE(cubic, nullptr);
    const RasterPosts posts = Posts(Heights());
    EXPECT_EQ(cubic->HeightAt(posts, 4.3, 17.2, 1.0),
              KernelUpsampler(ResampleMethod::Cubic).HeightAt(posts, 4.3, 17.2, 1.0));
}

TEST(ReadPosts, ReadsAnIndexBoxOfPosts) {
    const Raster<float> raster = Heights();
    const RasterPosts posts = Posts(raster);
    terraingen::detail::IndexBox box;
    box.Include(2, 4, 1, 3);
    box.Include(3, 5, 2, 2);
    EXPECT_EQ(box.Columns(), 4u);
    EXPECT_EQ(box.Rows(), 3u);
    EXPECT_EQ(box.Area(), 12.0);
    const auto window = terraingen::detail::ReadPosts(posts, box);
    EXPECT_EQ(window.width, 4u);
    EXPECT_EQ(window.At(3, 2), raster(3, 2));
    EXPECT_EQ(window.At(5, 3), raster(5, 3));
}

TEST(MarkMissing, FillsNaNCellsWithTheNoDataValue) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Raster<float> heights(2, 1);
    heights(0, 0) = 1.0f;
    heights(1, 0) = nan;
    terraingen::MarkMissing(heights, -9999.0f);
    EXPECT_EQ(heights(1, 0), -9999.0f);
    EXPECT_EQ(heights.NoData(), -9999.0f);
    heights(1, 0) = nan;
    terraingen::MarkMissing(heights, std::nullopt);
    EXPECT_TRUE(std::isnan(heights(1, 0)));
    ASSERT_TRUE(heights.NoData().has_value());
    EXPECT_TRUE(std::isnan(*heights.NoData()));
    Raster<float> full(1, 1, 2.0f);
    terraingen::MarkMissing(full, std::nullopt);
    EXPECT_FALSE(full.NoData().has_value());
}

TEST(PostIndex, WrapsThenClampsToThePostsHeld) {
    EXPECT_EQ(terraingen::detail::PostIndex(5, false, 10, 2, 8), 5u);
    EXPECT_EQ(terraingen::detail::PostIndex(-3, false, 10, 2, 8), 2u);
    EXPECT_EQ(terraingen::detail::PostIndex(12, false, 10, 2, 8), 7u);
    EXPECT_EQ(terraingen::detail::PostIndex(-3, true, 10, 0, 10), 7u);
    EXPECT_EQ(terraingen::detail::PostIndex(23, true, 10, 0, 10), 3u);
}

TEST(OffsetOnLattice, FindsGridsOnTheLatticesCells) {
    const GeoTransform lattice = GeoTransform::NorthUp(-6.0, 30.0, 3.0);
    const auto on =
        terraingen::detail::OffsetOnLattice(lattice, GeoTransform::NorthUp(3.0, 24.0, 3.0));
    ASSERT_TRUE(on.has_value());
    EXPECT_EQ(on->col, 3);
    EXPECT_EQ(on->row, 2);
    EXPECT_FALSE(
        terraingen::detail::OffsetOnLattice(lattice, GeoTransform::NorthUp(4.0, 24.0, 3.0)));
    EXPECT_FALSE(
        terraingen::detail::OffsetOnLattice(lattice, GeoTransform::NorthUp(3.0, 24.0, 1.5)));
}

TEST(Upsampler, HeightsAtGiveEachPointsHeightToTheBit) {
    const Raster<float> raster = Heights();
    const RasterPosts posts = Posts(raster);
    std::vector<terraingen::Vec2d> close;
    for (double x = -3.0; x < 28.0; x += 1.37) {
        close.push_back({x, 17.3});
        close.push_back({x, 9.1});
    }
    const std::vector<terraingen::Vec2d> far{{-3.0, 4.0}, {1000.0, -500.0}, {12.5, 18.5}};
    const MeanUpsampler mean;
    for (const auto& points : {close, far}) {
        for (const ResampleMethod method :
             {ResampleMethod::Nearest, ResampleMethod::Cubic, ResampleMethod::Lanczos3}) {
            const KernelUpsampler kernel(method);
            std::vector<float> heights(points.size());
            kernel.HeightsAt(posts, terraingen::Span<const terraingen::Vec2d>(points),
                             terraingen::Span<float>(heights), 0.5);
            for (std::size_t i = 0; i < points.size(); ++i) {
                ASSERT_TRUE(
                    SameBits(heights[i], kernel.HeightAt(posts, points[i].x, points[i].y, 0.5)))
                    << i;
            }
        }
        std::vector<float> upsampled(points.size());
        mean.HeightsAt(posts, terraingen::Span<const terraingen::Vec2d>(points),
                       terraingen::Span<float>(upsampled), 3.0);
        for (std::size_t i = 0; i < points.size(); ++i) {
            EXPECT_TRUE(SameBits(upsampled[i], mean.HeightAt(posts, points[i].x, points[i].y, 3.0)))
                << i;
        }
    }
}

TEST(Upsampler, CopiesTheLatticesPostsWithoutUpsampling) {
    const Raster<float> raster = Heights();
    const RasterPosts posts = Posts(raster);
    const ThreadNotingUpsampler noting;
    Raster<float> filled = RasterGrid{6, 4, GeoTransform::NorthUp(0.0, 27.0, 3.0)}.MakeRaster();
    noting.Fill(posts, filled, 3.0, 0);
    EXPECT_TRUE(noting.Threads().empty()) << "no cell is upsampled";
    for (std::size_t row = 0; row < filled.Height(); ++row) {
        for (std::size_t col = 0; col < filled.Width(); ++col) {
            EXPECT_EQ(filled(col, row), raster(col + 2, row + 1));
        }
    }
}
