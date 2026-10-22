#include "terraingen/sources/diamond_square.h"

#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::DiamondSquareInterp;
using terraingen::DiamondSquareOptions;
using terraingen::DiamondSquareUpsampler;
using terraingen::EstimateMidpointRoughness;
using terraingen::GeoTransform;
using terraingen::MidpointNodes;
using terraingen::Raster;
using terraingen::RasterGrid;

namespace {

/// Posts 4 m apart with post (0, 0) at (2, -6), holding a function of their position.
class FunctionPosts final : public terraingen::PostLattice {
  public:
    explicit FunctionPosts(std::function<float(double, double)> height)
        : height_(std::move(height)) {}

    GeoTransform Transform() const override {
        return GeoTransform::NorthUp(0.0, -4.0, 4.0);
    }

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              terraingen::Span<float> heights) const override {
        const GeoTransform transform = Transform();
        for (std::size_t r = 0; r < height; ++r) {
            for (std::size_t c = 0; c < width; ++c) {
                const terraingen::Vec2d p = transform.PixelToWorld(
                    static_cast<double>(col0 + static_cast<std::int64_t>(c)) + 0.5,
                    static_cast<double>(row0 + static_cast<std::int64_t>(r)) + 0.5);
                heights[r * width + c] = height_(p.x, p.y);
            }
        }
    }

  private:
    std::function<float(double, double)> height_;
};

/// Gentle hills, with no data west of x = -1000.
float Hills(double x, double y) {
    if (x < -1000.0) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>(10.0 * std::sin(x / 37.0) * std::cos(y / 23.0));
}

DiamondSquareOptions Options(DiamondSquareInterp interp = DiamondSquareInterp::Bilinear) {
    DiamondSquareOptions options;
    options.fractalDimension = 2.3;
    options.scale = 0.5;
    options.interp = interp;
    options.seed = 7;
    return options;
}

/// A north-up grid of `width` x `height` cells of `step` with cell centers on the nodes of
/// spacing `step` from (2, -6), its first column at node `col0` and its top row at node `rowTop`.
RasterGrid NodeGrid(double step, std::int64_t col0, std::int64_t rowTop, std::size_t width,
                    std::size_t height) {
    return {width, height,
            GeoTransform::NorthUp(2.0 + (static_cast<double>(col0) - 0.5) * step,
                                  -6.0 + (static_cast<double>(rowTop) + 0.5) * step, step)};
}

/// `grid` filled by `upsampler` from `posts` at the grid's spacing.
Raster<float> Fill(const DiamondSquareUpsampler& upsampler, const terraingen::PostLattice& posts,
                   const RasterGrid& grid) {
    Raster<float> heights = grid.MakeRaster();
    upsampler.Fill(posts, heights, grid.PixelSize());
    return heights;
}

/// The 40 x 30 DEM of 4 m cells the midpoint displacement layer this upsampler replaces was
/// checked against.
Raster<float> GoldenDem() {
    Raster<float> dem(40, 30);
    dem.SetTransform(GeoTransform::NorthUp(100.0, 500.0, 4.0));
    for (std::size_t row = 0; row < 30; ++row) {
        for (std::size_t col = 0; col < 40; ++col) {
            const terraingen::Vec2d p = dem.CellCenter(col, row);
            dem(col, row) = static_cast<float>(20.0 * std::sin(p.x / 53.0) * std::cos(p.y / 41.0) +
                                               0.3 * std::sin(p.x * 1.7 + p.y * 0.9));
        }
    }
    return dem;
}

}  // namespace

TEST(DiamondSquareOptions, RejectInvalidValues) {
    EXPECT_NO_THROW(terraingen::ValidateDiamondSquareOptions(Options()));
    EXPECT_NO_THROW(terraingen::ValidateDiamondSquareOptions(DiamondSquareOptions{}));
    const auto rejects = [](auto change) {
        DiamondSquareOptions options = Options();
        change(options);
        EXPECT_THROW(terraingen::ValidateDiamondSquareOptions(options), std::invalid_argument);
        EXPECT_THROW(DiamondSquareUpsampler{options}, std::invalid_argument);
    };
    rejects([](DiamondSquareOptions& o) { o.fractalDimension = 1.9; });
    rejects([](DiamondSquareOptions& o) { o.fractalDimension = 3.1; });
    rejects([](DiamondSquareOptions& o) { o.fractalDimension = std::nan(""); });
    rejects([](DiamondSquareOptions& o) { o.scale = -1.0; });
    rejects([](DiamondSquareOptions& o) { o.minPixelSize = -1.0; });
}

TEST(DiamondSquareOptions, CompareMemberwise) {
    DiamondSquareOptions other = Options();
    EXPECT_EQ(Options(), other);
    other.seed = 8;
    EXPECT_NE(Options(), other);
    EXPECT_NE(Options(), Options(DiamondSquareInterp::CatmullRom));
}

TEST(DiamondSquareUpsampler, RefinesAsManyLevelsAsTheSpacingResolves) {
    const DiamondSquareUpsampler upsampler(Options());
    EXPECT_EQ(upsampler.Levels(4.0, 8.0), 0);
    EXPECT_EQ(upsampler.Levels(4.0, 4.0), 0);
    EXPECT_EQ(upsampler.Levels(4.0, 3.0), 1);
    EXPECT_EQ(upsampler.Levels(4.0, 2.0), 1);
    EXPECT_EQ(upsampler.Levels(4.0, 1.0), 2);
    EXPECT_EQ(upsampler.Levels(4.0, 0.01), 9);
    DiamondSquareOptions capped = Options();
    capped.minPixelSize = 1.0;
    EXPECT_EQ(DiamondSquareUpsampler(capped).Levels(4.0, 0.01), 2);
    capped.minPixelSize = 8.0;
    EXPECT_EQ(DiamondSquareUpsampler(capped).Levels(4.0, 0.01), 0);
    EXPECT_EQ(upsampler.Reach(), 4);
    EXPECT_EQ(DiamondSquareUpsampler(Options(DiamondSquareInterp::CatmullRom)).Reach(), 8);
}

TEST(DiamondSquareUpsampler, KeepsEveryCoarserNodeAndAgreesAcrossWindows) {
    const FunctionPosts posts(Hills);
    for (const DiamondSquareInterp interp :
         {DiamondSquareInterp::Bilinear, DiamondSquareInterp::CatmullRom}) {
        const DiamondSquareUpsampler upsampler(Options(interp));
        const MidpointNodes base = upsampler.Nodes(posts, 0, -3, -2, 4, 5);
        EXPECT_EQ(base.At(1, 2), static_cast<double>(Hills(6.0, 2.0)))
            << "node (1, 2) is 1 post east and 2 north of post (0, 0)";
        const MidpointNodes coarse = upsampler.Nodes(posts, 2, -12, -8, 16, 20);
        const MidpointNodes fine = upsampler.Nodes(posts, 3, -24, -16, 32, 40);
        for (std::int64_t row = -2; row <= 5; ++row) {
            for (std::int64_t col = -3; col <= 4; ++col) {
                EXPECT_EQ(coarse.At(4 * col, 4 * row), base.At(col, row));
            }
        }
        for (std::int64_t row = -8; row <= 20; ++row) {
            for (std::int64_t col = -12; col <= 16; ++col) {
                EXPECT_EQ(fine.At(2 * col, 2 * row), coarse.At(col, row));
            }
        }
        const MidpointNodes shifted = upsampler.Nodes(posts, 3, 5, 7, 9, 12);
        for (std::int64_t row = 7; row <= 12; ++row) {
            for (std::int64_t col = 5; col <= 9; ++col) {
                EXPECT_EQ(shifted.At(col, row), fine.At(col, row));
            }
        }
    }
}

TEST(DiamondSquareUpsampler, DisplacesNewNodesByTheStepScale) {
    const FunctionPosts flat([](double, double) { return 0.0f; });
    const DiamondSquareOptions options = Options();
    const DiamondSquareUpsampler upsampler(options);
    const MidpointNodes nodes = upsampler.Nodes(flat, 1, -200, -200, 200, 200);
    double squares = 0.0;
    std::size_t count = 0;
    for (std::int64_t row = -199; row <= 199; row += 2) {
        for (std::int64_t col = -199; col <= 199; col += 2) {
            squares += nodes.At(col, row) * nodes.At(col, row);
            ++count;
        }
        for (std::int64_t col = -200; col <= 200; col += 2) {
            EXPECT_EQ(nodes.At(col, row - 1), 0.0) << "the posts stay";
        }
    }
    const double expected =
        *options.scale * std::pow(2.0, -(3.0 - *options.fractalDimension) / 2.0);
    EXPECT_NEAR(std::sqrt(squares / static_cast<double>(count)), expected, 0.03 * expected);
}

TEST(DiamondSquareUpsampler, InterpolatesThePostsAtTheirSpacingOrCoarser) {
    const FunctionPosts posts(Hills);
    const DiamondSquareUpsampler upsampler(Options());
    const RasterGrid grid = NodeGrid(4.0, -5, 5, 10, 10);
    const Raster<float> heights = Fill(upsampler, posts, grid);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const terraingen::Vec2d p = heights.CellCenter(col, row);
            EXPECT_EQ(heights(col, row), Hills(p.x, p.y));
        }
    }
    const double between =
        0.5 * (static_cast<double>(Hills(2.0, -6.0)) + static_cast<double>(Hills(6.0, -6.0)));
    EXPECT_NEAR(upsampler.HeightAt(posts, 4.0, -6.0, 8.0), between, 1e-5)
        << "bilinear between the posts";
}

TEST(DiamondSquareUpsampler, NestedGridsShareTheCoarserGridsCells) {
    const FunctionPosts posts(Hills);
    for (const DiamondSquareInterp interp :
         {DiamondSquareInterp::Bilinear, DiamondSquareInterp::CatmullRom}) {
        const DiamondSquareUpsampler upsampler(Options(interp));
        const RasterGrid coarse = NodeGrid(1.0, -40, 40, 81, 81);
        const RasterGrid fine = NodeGrid(0.5, -40, 40, 81, 81);
        const Raster<float> outer = Fill(upsampler, posts, coarse);
        const Raster<float> inner = Fill(upsampler, posts, fine);
        for (std::size_t row = 0; row < inner.Height(); row += 2) {
            for (std::size_t col = 0; col < inner.Width(); col += 2) {
                ASSERT_EQ(inner(col, row), outer(20 + col / 2, 20 + row / 2));
            }
        }
        double change = 0.0;
        for (std::size_t row = 0; row < outer.Height(); ++row) {
            for (std::size_t col = 0; col < outer.Width(); ++col) {
                const terraingen::Vec2d p = outer.CellCenter(col, row);
                change = std::max(change, std::fabs(static_cast<double>(outer(col, row)) -
                                                    static_cast<double>(Hills(p.x, p.y))));
            }
        }
        EXPECT_GT(change, 0.1) << "the refinement adds relief";
    }
}

TEST(DiamondSquareUpsampler, PointsAndTilesMatchAGridOffTheLattice) {
    const FunctionPosts posts(Hills);
    for (const DiamondSquareInterp interp :
         {DiamondSquareInterp::Bilinear, DiamondSquareInterp::CatmullRom}) {
        const DiamondSquareUpsampler upsampler(Options(interp));
        const RasterGrid grid{300, 140, GeoTransform::NorthUp(-31.3, 17.9, 0.7)};
        const Raster<float> whole = Fill(upsampler, posts, grid);
        const RasterGrid tile = terraingen::Subgrid(grid, {130, 170, 120, 140});
        const Raster<float> part = Fill(upsampler, posts, tile);
        for (std::size_t row = 0; row < part.Height(); ++row) {
            for (std::size_t col = 0; col < part.Width(); ++col) {
                ASSERT_EQ(part(col, row), whole(130 + col, 120 + row));
            }
        }
        for (std::size_t row = 0; row < grid.height; row += 13) {
            for (std::size_t col = 0; col < grid.width; col += 11) {
                const terraingen::Vec2d p = whole.CellCenter(col, row);
                ASSERT_EQ(upsampler.HeightAt(posts, p.x, p.y, 0.7), whole(col, row));
            }
        }
    }
}

TEST(DiamondSquareUpsampler, CatmullRomFollowsCurvedPostsWhereBilinearCutsCorners) {
    const FunctionPosts bowl([](double x, double) { return static_cast<float>(x * x / 16.0); });
    DiamondSquareOptions smooth = Options();
    smooth.scale = 0.0;
    const DiamondSquareUpsampler bilinear(smooth);
    smooth.interp = DiamondSquareInterp::CatmullRom;
    const DiamondSquareUpsampler spline(smooth);
    const double x = 2.0 + 4.0 * 3.0 + 2.0;
    const double exact = x * x / 16.0;
    EXPECT_NEAR(spline.HeightAt(bowl, x, -6.0, 2.0), exact, 1e-4);
    EXPECT_NEAR(bilinear.HeightAt(bowl, x, -6.0, 2.0), exact + 0.25, 1e-4);
    EXPECT_NEAR(spline.HeightAt(bowl, x + 0.5, -6.0, 0.25), (x + 0.5) * (x + 0.5) / 16.0, 1e-4);
}

TEST(DiamondSquareUpsampler, GivesNaNWhereThePostsHaveNoData) {
    const FunctionPosts posts(Hills);
    const DiamondSquareUpsampler upsampler(Options());
    EXPECT_TRUE(std::isnan(upsampler.HeightAt(posts, -1003.0, 0.5, 1.0)));
    EXPECT_FALSE(std::isnan(upsampler.HeightAt(posts, 10.3, 0.5, 1.0)));
}

TEST(DiamondSquareUpsampler, NeedsItsRoughnessAndAnUnrotatedLattice) {
    const FunctionPosts posts(Hills);
    const DiamondSquareUpsampler unfitted{DiamondSquareOptions{}};
    EXPECT_THROW(static_cast<void>(unfitted.HeightAt(posts, 1.0, 1.0, 1.0)), std::invalid_argument);
    EXPECT_THROW(unfitted.Nodes(posts, 1, 0, 0, 1, 1), std::invalid_argument);
    Raster<float> rotated(8, 8, 1.0f);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    const auto shared = std::make_shared<const Raster<float>>(rotated);
    const terraingen::RasterPosts turned(shared, terraingen::RasterPlacement::Whole(*shared));
    EXPECT_THROW(
        static_cast<void>(DiamondSquareUpsampler(Options()).HeightAt(turned, 2.0, -2.0, 0.1)),
        std::invalid_argument);
}

TEST(DiamondSquareUpsampler, FitsAnUnsetRoughnessToThePosts) {
    const DiamondSquareUpsampler given(Options());
    const FunctionPosts posts(Hills);
    EXPECT_EQ(given.FitTo(posts), nullptr);

    for (const double dimension : {2.2, 2.6}) {
        DiamondSquareOptions source = Options();
        source.fractalDimension = dimension;
        source.scale = 2.0;
        const DiamondSquareUpsampler expander(source);
        const FunctionPosts flat([](double, double) { return 0.0f; });
        constexpr int kLevels = 6;
        const MidpointNodes nodes = expander.Nodes(flat, kLevels, 0, 0, 511, 511);
        auto raster = std::make_shared<Raster<float>>(nodes.width, nodes.height);
        raster->SetTransform(GeoTransform::NorthUp(0.0, 0.0, 1.0));
        for (std::size_t i = 0; i < nodes.values.size(); ++i) {
            (*raster)(i % nodes.width, i / nodes.width) = static_cast<float>(nodes.values[i]);
        }
        const terraingen::RasterPosts expanded(raster, terraingen::RasterPlacement::Whole(*raster));
        DiamondSquareOptions unset;
        unset.seed = 9;
        const auto fitted = std::dynamic_pointer_cast<const DiamondSquareUpsampler>(
            DiamondSquareUpsampler(unset).FitTo(expanded));
        ASSERT_NE(fitted, nullptr);
        const double last = 2.0 * std::pow(2.0, -(3.0 - dimension) * kLevels);
        EXPECT_NEAR(*fitted->Options().scale, last, 0.25 * last) << dimension;
        EXPECT_NEAR(*fitted->Options().fractalDimension, dimension, 0.3) << dimension;
        EXPECT_EQ(fitted->Options().seed, 9u);
        DiamondSquareOptions half;
        half.scale = 1.0;
        const auto partly = std::dynamic_pointer_cast<const DiamondSquareUpsampler>(
            DiamondSquareUpsampler(half).FitTo(expanded));
        EXPECT_EQ(*partly->Options().scale, 1.0) << "a given scale is kept";
    }

    const auto unbounded = std::dynamic_pointer_cast<const DiamondSquareUpsampler>(
        DiamondSquareUpsampler{DiamondSquareOptions{}}.FitTo(posts));
    ASSERT_NE(unbounded, nullptr);
    EXPECT_GT(*unbounded->Options().scale, 0.0);
}

TEST(DiamondSquareUpsampler, MatchesTheMidpointDisplacementLayerItReplaces) {
    const auto dem = std::make_shared<const Raster<float>>(GoldenDem());
    DiamondSquareOptions options;
    options.fractalDimension = 2.3;
    options.scale = 0.5;
    options.seed = 7;
    terraingen::RasterSourceOptions sampling;
    sampling.upsampler = std::make_shared<DiamondSquareUpsampler>(options);
    const terraingen::RasterSource source(dem, sampling);

    Raster<float> first = RasterGrid{24, 20, GeoTransform::NorthUp(130.0, 470.0, 1.0)}.MakeRaster();
    source.Fill(first, 1.0);
    double sum = 0.0;
    for (const float value : first.Values()) {
        sum += static_cast<double>(value);
    }
    EXPECT_EQ(sum, 908.09665554302046);
    EXPECT_EQ(first(0, 0), 5.34079409f);
    EXPECT_EQ(first(9, 0), 4.24850416f);
    EXPECT_EQ(first(18, 7), 1.48185813f);
    EXPECT_EQ(first(9, 14), 1.27179337f);

    Raster<float> second =
        RasterGrid{30, 25, GeoTransform::NorthUp(131.3, 471.9, 0.7)}.MakeRaster();
    source.Fill(second, 0.7);
    sum = 0.0;
    for (const float value : second.Values()) {
        sum += static_cast<double>(value);
    }
    EXPECT_EQ(sum, 1939.871339827776);
    EXPECT_EQ(second(0, 0), 5.92824841f);
    EXPECT_EQ(second(27, 0), 3.11245632f);
    EXPECT_EQ(second(18, 14), 2.54675436f);
    EXPECT_EQ(second(27, 21), 1.10294878f);

    EXPECT_EQ(source.HeightAt(140.17, 450.33, 0.3), -0.215913281f);
    EXPECT_EQ(source.HeightAt(161.9, 461.01, 0.3), 0.50187993f);
    EXPECT_EQ(source.HeightAt(150.0, 430.0, 0.3), -2.71018195f);
}

TEST(EstimateMidpointRoughness, HandlesFlatGroundNoDataAndSmallRasters) {
    Raster<float> flat(8, 8, 3.0f);
    const auto roughness = EstimateMidpointRoughness(flat);
    EXPECT_EQ(roughness.scale, 0.0);
    EXPECT_EQ(roughness.fractalDimension, 2.5);
    flat.SetNoData(-9999.0f);
    flat(4, 4) = -9999.0f;
    EXPECT_NO_THROW(EstimateMidpointRoughness(flat));
    EXPECT_THROW(EstimateMidpointRoughness(Raster<float>(2, 2, 0.0f)), std::invalid_argument);
}

TEST(EstimateMidpointRoughness, RecoversTheDimensionOfFractionalBrownianSurfaces) {
    constexpr std::size_t kSize = 256;
    for (const auto [dimension, tolerance] : {std::pair{2.2, 0.1}, {2.5, 0.1}, {2.8, 0.2}}) {
        const double hurst = 3.0 - dimension;
        std::vector<std::complex<double>> spectrum(kSize * kSize);
        for (std::size_t v = 0; v < kSize; ++v) {
            for (std::size_t u = 0; u < kSize; ++u) {
                const double ku = static_cast<double>(u <= kSize / 2 ? u : kSize - u);
                const double kv = static_cast<double>(v <= kSize / 2 ? v : kSize - v);
                const double k = std::hypot(ku, kv);
                if (k == 0.0) {
                    continue;
                }
                terraingen::RandomStream random(3, 0, static_cast<std::uint32_t>(u),
                                                static_cast<std::uint32_t>(v));
                const double amplitude =
                    std::pow(k, -(hurst + 1.0)) * terraingen::StandardNormal(random);
                const double phase = 2.0 * terraingen::kPi * random.NextDouble();
                spectrum[v * kSize + u] = {amplitude * std::cos(phase),
                                           amplitude * std::sin(phase)};
            }
        }
        std::vector<std::complex<double>> line(kSize);
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t a = 0; a < kSize; ++a) {
                for (std::size_t b = 0; b < kSize; ++b) {
                    line[b] = pass == 0 ? spectrum[a * kSize + b] : spectrum[b * kSize + a];
                }
                terraingen::Fft(line, 1.0);
                for (std::size_t b = 0; b < kSize; ++b) {
                    (pass == 0 ? spectrum[a * kSize + b] : spectrum[b * kSize + a]) = line[b];
                }
            }
        }
        Raster<float> heights(kSize, kSize);
        for (std::size_t i = 0; i < spectrum.size(); ++i) {
            heights(i % kSize, i / kSize) = static_cast<float>(spectrum[i].real());
        }
        EXPECT_NEAR(EstimateMidpointRoughness(heights).fractalDimension, dimension, tolerance)
            << dimension;
    }
}
