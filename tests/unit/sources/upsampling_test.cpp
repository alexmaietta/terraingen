#include "support/raster_equality.h"
#include "terraingen/sources/upsampling.h"

#include "terraingen/raster/resample.h"
#include "terraingen/sources/raster_source.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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
    float HeightAt(const terraingen::PostLattice& posts, double x, double y,
                   double /*spacing*/) const override {
        const terraingen::Vec2d pixel = posts.Transform().WorldToPixel(x, y);
        std::vector<float> values(4);
        posts.Read(static_cast<std::int64_t>(std::floor(pixel.x - 0.5)),
                   static_cast<std::int64_t>(std::floor(pixel.y - 0.5)), 2, 2,
                   terraingen::Span<float>(values));
        return 0.25f * (values[0] + values[1] + values[2] + values[3]);
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

TEST(KernelUpsampler, GivesSampleRastersBitsForEveryKernel) {
    const Raster<float> raster = Heights();
    const RasterPosts posts = Posts(raster);
    for (const ResampleMethod method :
         {ResampleMethod::Nearest, ResampleMethod::Bilinear, ResampleMethod::Cubic,
          ResampleMethod::CatmullRom, ResampleMethod::Lanczos3, ResampleMethod::CubicBSpline}) {
        const KernelUpsampler upsampler(method);
        EXPECT_EQ(upsampler.Method(), method);
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
        upsampler.Fill(posts, filled, 0.77);
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
    cubic.Fill(posts, filled, 400.0);
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

TEST(UpsampledHeight, GivesThePostsOwnHeightAtTheirSpacingOrCoarser) {
    const CountingPosts posts;
    const MeanUpsampler mean;
    EXPECT_TRUE(terraingen::AtPostSpacing(posts.Transform(), 2.0));
    EXPECT_TRUE(terraingen::AtPostSpacing(posts.Transform(), 2.0 * (1.0 - 1e-12)));
    EXPECT_FALSE(terraingen::AtPostSpacing(posts.Transform(), 1.9));
    EXPECT_EQ(terraingen::UpsampledHeight(mean, posts, 3.0, -5.0, 2.0), 1.0f + 200.0f);
    EXPECT_EQ(terraingen::UpsampledHeight(mean, posts, 3.0, -5.0, 1.0),
              mean.HeightAt(posts, 3.0, -5.0, 1.0));
    EXPECT_EQ(terraingen::UpsampledHeight(mean, posts, 3.5, -5.0, 2.0),
              mean.HeightAt(posts, 3.5, -5.0, 2.0));
    const RasterGrid grid{5, 4, GeoTransform::NorthUp(0.0, 0.0, 2.0)};
    for (const double spacing : {2.0, 0.5}) {
        Raster<float> filled = grid.MakeRaster();
        terraingen::FillUpsampled(mean, posts, filled, spacing);
        for (std::size_t row = 0; row < filled.Height(); ++row) {
            for (std::size_t col = 0; col < filled.Width(); ++col) {
                const terraingen::Vec2d center = filled.CellCenter(col, row);
                EXPECT_EQ(filled(col, row),
                          terraingen::UpsampledHeight(mean, posts, center.x, center.y, spacing));
            }
        }
    }
    EXPECT_DOUBLE_EQ(terraingen::PostSpacing(GeoTransform{0.0, 2.0, 0.0, 0.0, 0.0, -5.0}), 5.0);
}

TEST(Upsampler, FillsCellByCellByDefaultAndFitsNothing) {
    const CountingPosts posts;
    const MeanUpsampler mean;
    EXPECT_EQ(mean.Reach(), 2);
    EXPECT_EQ(mean.FitTo(posts), nullptr);
    Raster<float> filled = RasterGrid{3, 3, GeoTransform::NorthUp(1.0, -1.0, 1.0)}.MakeRaster();
    mean.Fill(posts, filled, 1.0);
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
    EXPECT_EQ(cubic->Method(), ResampleMethod::Cubic);
}
