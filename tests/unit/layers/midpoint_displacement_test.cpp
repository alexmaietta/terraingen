#include "terraingen/layers/midpoint_displacement.h"

#include "terraingen/layers/sample_block.h"

#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::EstimateMidpointRoughness;
using terraingen::GeoTransform;
using terraingen::MidpointBase;
using terraingen::MidpointDisplacementLayer;
using terraingen::MidpointDisplacementOptions;
using terraingen::MidpointNodes;
using terraingen::Raster;
using terraingen::RasterGrid;

namespace {

/// Gentle hills, with no data west of x = -1000.
class Hills final : public terraingen::Ground {
  public:
    float HeightAt(double x, double y, double) const override {
        if (x < -1000.0) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return static_cast<float>(10.0 * std::sin(x / 37.0) * std::cos(y / 23.0));
    }
};

MidpointDisplacementOptions Options(MidpointBase base = MidpointBase::DiamondSquare) {
    MidpointDisplacementOptions options;
    options.pixelSize = 4.0;
    options.origin = {2.0, -6.0};
    options.fractalDimension = 2.3;
    options.scale = 0.5;
    options.base = base;
    options.seed = 7;
    return options;
}

/// A north-up grid of `width` x `height` cells of `step` with cell centres on the nodes of
/// spacing `step` from `origin`, its first column at node `col0` and its top row at node `rowTop`.
RasterGrid NodeGrid(const terraingen::Vec2d& origin, double step, std::int64_t col0,
                    std::int64_t rowTop, std::size_t width, std::size_t height) {
    return {width, height,
            GeoTransform::NorthUp(origin.x + (static_cast<double>(col0) - 0.5) * step,
                                  origin.y + (static_cast<double>(rowTop) + 0.5) * step, step)};
}

/// `grid` evaluated by `layer` over `ground` at its own spacing.
Raster<float> Evaluate(const MidpointDisplacementLayer& layer, const terraingen::Ground& ground,
                       const RasterGrid& grid, unsigned threads = 1) {
    Raster<float> heights = grid.MakeRaster();
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const terraingen::Vec2d p = heights.CellCenter(col, row);
            heights(col, row) = ground.HeightAt(p.x, p.y, 0.0);
        }
    }
    terraingen::LayerContext context;
    context.threads = threads;
    context.ground = &ground;
    layer.Apply(heights, context);
    return heights;
}

}  // namespace

TEST(MidpointDisplacementOptions, RejectInvalidValues) {
    EXPECT_NO_THROW(terraingen::ValidateMidpointDisplacementOptions(Options()));
    const auto rejects = [](auto change) {
        MidpointDisplacementOptions options = Options();
        change(options);
        EXPECT_THROW(terraingen::ValidateMidpointDisplacementOptions(options),
                     std::invalid_argument);
        EXPECT_THROW(MidpointDisplacementLayer{options}, std::invalid_argument);
    };
    rejects([](MidpointDisplacementOptions& o) { o.pixelSize = 0.0; });
    rejects([](MidpointDisplacementOptions& o) { o.origin.x = std::nan(""); });
    rejects([](MidpointDisplacementOptions& o) {
        o.origin.y = std::numeric_limits<double>::infinity();
    });
    rejects([](MidpointDisplacementOptions& o) { o.fractalDimension = 1.9; });
    rejects([](MidpointDisplacementOptions& o) { o.fractalDimension = 3.1; });
    rejects([](MidpointDisplacementOptions& o) { o.fractalDimension = std::nan(""); });
    rejects([](MidpointDisplacementOptions& o) { o.scale = -1.0; });
    rejects([](MidpointDisplacementOptions& o) { o.minPixelSize = -1.0; });
}

TEST(MidpointDisplacementOptions, CompareMemberwise) {
    MidpointDisplacementOptions other = Options();
    EXPECT_EQ(Options(), other);
    other.seed = 8;
    EXPECT_NE(Options(), other);
}

TEST(MidpointDisplacementLayer, ExpandsAsManyLevelsAsTheSpacingResolves) {
    const MidpointDisplacementLayer layer(Options());
    EXPECT_EQ(layer.Levels(8.0), 0);
    EXPECT_EQ(layer.Levels(4.0), 0);
    EXPECT_EQ(layer.Levels(3.0), 1);
    EXPECT_EQ(layer.Levels(2.0), 1);
    EXPECT_EQ(layer.Levels(1.0), 2);
    EXPECT_EQ(layer.Levels(0.01), 9);
    EXPECT_DOUBLE_EQ(layer.NodeSpacing(2), 1.0);

    MidpointDisplacementOptions capped = Options();
    capped.minPixelSize = 1.0;
    EXPECT_EQ(MidpointDisplacementLayer(capped).Levels(0.01), 2);
    capped.minPixelSize = 8.0;
    EXPECT_EQ(MidpointDisplacementLayer(capped).Levels(0.01), 0);
}

TEST(MidpointDisplacementLayer, KeepsEveryCoarserNodeAndAgreesAcrossWindows) {
    const Hills ground;
    const MidpointDisplacementLayer layer(Options());
    const MidpointNodes base = layer.Nodes(0, -3, -2, 4, 5, &ground);
    EXPECT_DOUBLE_EQ(base.At(1, 2), static_cast<double>(ground.HeightAt(6.0, 2.0, 4.0)));
    const MidpointNodes coarse = layer.Nodes(2, -12, -8, 16, 20, &ground);
    const MidpointNodes fine = layer.Nodes(3, -24, -16, 32, 40, &ground);
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
    const MidpointNodes shifted = layer.Nodes(3, 5, 7, 9, 12, &ground);
    for (std::int64_t row = 7; row <= 12; ++row) {
        for (std::int64_t col = 5; col <= 9; ++col) {
            EXPECT_EQ(shifted.At(col, row), fine.At(col, row));
        }
    }
}

TEST(MidpointDisplacementLayer, DisplacesNewNodesByTheStepScale) {
    MidpointDisplacementOptions options = Options(MidpointBase::Source);
    const MidpointDisplacementLayer layer(options);
    const MidpointNodes nodes = layer.Nodes(1, -200, -200, 200, 200, nullptr);
    double squares = 0.0;
    std::size_t count = 0;
    for (std::int64_t row = -199; row <= 199; row += 2) {
        for (std::int64_t col = -199; col <= 199; col += 2) {
            squares += nodes.At(col, row) * nodes.At(col, row);
            ++count;
        }
        for (std::int64_t col = -200; col <= 200; col += 2) {
            EXPECT_EQ(nodes.At(col, row - 1), 0.0) << "base nodes stay zero";
        }
    }
    const double expected = options.scale * std::pow(2.0, -(3.0 - options.fractalDimension) / 2.0);
    EXPECT_NEAR(std::sqrt(squares / static_cast<double>(count)), expected, 0.03 * expected);
}

TEST(MidpointDisplacementLayer, LeavesHeightsAloneAtTheBaseSpacingOrCoarser) {
    const Hills ground;
    const MidpointDisplacementLayer layer(Options());
    const RasterGrid grid = NodeGrid({2.0, -6.0}, 4.0, -5, 5, 10, 10);
    const Raster<float> heights = Evaluate(layer, ground, grid);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const terraingen::Vec2d p = heights.CellCenter(col, row);
            EXPECT_EQ(heights(col, row), ground.HeightAt(p.x, p.y, 0.0));
        }
    }
}

TEST(MidpointDisplacementLayer, NestedGridsShareTheCoarserGridsCells) {
    const Hills ground;
    const MidpointDisplacementLayer layer(Options());
    const terraingen::Vec2d origin{2.0, -6.0};
    const RasterGrid coarse = NodeGrid(origin, 1.0, -40, 40, 81, 81);
    const RasterGrid fine = NodeGrid(origin, 0.5, -40, 40, 81, 81);
    const Raster<float> outer = Evaluate(layer, ground, coarse, 4);
    const Raster<float> inner = Evaluate(layer, ground, fine, 4);
    for (std::size_t row = 0; row < inner.Height(); row += 2) {
        for (std::size_t col = 0; col < inner.Width(); col += 2) {
            ASSERT_EQ(inner(col, row), outer(20 + col / 2, 20 + row / 2));
        }
    }
    double change = 0.0;
    for (std::size_t row = 0; row < outer.Height(); ++row) {
        for (std::size_t col = 0; col < outer.Width(); ++col) {
            const terraingen::Vec2d p = outer.CellCenter(col, row);
            change =
                std::max(change, std::fabs(static_cast<double>(outer(col, row)) -
                                           static_cast<double>(ground.HeightAt(p.x, p.y, 0.0))));
        }
    }
    EXPECT_GT(change, 0.1) << "the expansion adds relief";
}

TEST(MidpointDisplacementLayer, PointsAndThreadedTilesMatchAGridOffTheLattice) {
    const Hills ground;
    for (MidpointBase base : {MidpointBase::DiamondSquare, MidpointBase::Source}) {
        const MidpointDisplacementLayer layer(Options(base));
        RasterGrid grid{300, 140, GeoTransform::NorthUp(-31.3, 17.9, 0.7)};
        const Raster<float> one = Evaluate(layer, ground, grid, 1);
        const Raster<float> many = Evaluate(layer, ground, grid, 8);
        terraingen::LayerContext context;
        context.spacing = 0.7;
        context.ground = &ground;
        for (std::size_t row = 0; row < grid.height; row += 13) {
            for (std::size_t col = 0; col < grid.width; col += 11) {
                ASSERT_EQ(one(col, row), many(col, row));
                const terraingen::Vec2d p = one.CellCenter(col, row);
                const float before = ground.HeightAt(p.x, p.y, 0.0);
                ASSERT_EQ(layer.ApplyAt(before, p.x, p.y, context), one(col, row));
            }
        }
    }
}

TEST(MidpointDisplacementLayer, KeepsSamplesWhereTheGroundHasNoData) {
    const Hills ground;
    const MidpointDisplacementLayer layer(Options());
    terraingen::LayerContext context;
    context.spacing = 1.0;
    context.ground = &ground;
    EXPECT_EQ(layer.ApplyAt(3.0f, -1003.0, 0.5, context), 3.0f);
    EXPECT_NE(layer.ApplyAt(3.0f, 10.3, 0.5, context), 3.0f);
}

TEST(MidpointDisplacementLayer, NeedsTheGroundToExpandIt) {
    const MidpointDisplacementLayer layer(Options());
    terraingen::LayerContext context;
    context.spacing = 1.0;
    EXPECT_THROW(layer.ApplyAt(0.0f, 0.0, 0.0, context), std::invalid_argument);
    EXPECT_THROW(layer.Nodes(1, 0, 0, 1, 1, nullptr), std::invalid_argument);
    context.spacing = 8.0;
    EXPECT_EQ(layer.ApplyAt(2.0f, 0.0, 0.0, context), 2.0f) << "nothing to expand";
}

TEST(EstimateMidpointRoughness, RecoversTheScaleAndDimensionOfAnExpansion) {
    for (double dimension : {2.2, 2.5, 2.8}) {
        MidpointDisplacementOptions options = Options(MidpointBase::Source);
        options.fractalDimension = dimension;
        options.scale = 2.0;
        const MidpointDisplacementLayer layer(options);
        constexpr int kLevels = 6;
        const MidpointNodes nodes = layer.Nodes(kLevels, 0, 0, 511, 511, nullptr);
        Raster<float> heights(nodes.width, nodes.height);
        for (std::size_t i = 0; i < nodes.values.size(); ++i) {
            heights(i % nodes.width, i / nodes.width) = static_cast<float>(nodes.values[i]);
        }
        const auto roughness = EstimateMidpointRoughness(heights);
        const double last = options.scale * std::pow(2.0, -(3.0 - dimension) * kLevels);
        EXPECT_NEAR(roughness.scale, last, 0.25 * last) << dimension;
        EXPECT_NEAR(roughness.fractalDimension, dimension, 0.3) << dimension;
    }
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

TEST(MidpointDisplacementLayer, SkipsCellsAndPointsWithoutData) {
    const Hills ground;
    const MidpointDisplacementLayer layer(Options());
    Raster<float> heights = RasterGrid{200, 10, GeoTransform::NorthUp(0.0, 10.0, 1.0)}.MakeRaster();
    heights.SetNoData(-9999.0f);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            heights(col, row) = col < 128 ? -9999.0f : 1.0f;
        }
    }
    heights(150, 5) = std::numeric_limits<float>::quiet_NaN();
    terraingen::LayerContext context;
    context.ground = &ground;
    layer.Apply(heights, context);
    EXPECT_EQ(heights(0, 0), -9999.0f) << "a tile without data";
    EXPECT_TRUE(std::isnan(heights(150, 5)));
    EXPECT_NE(heights(151, 5), 1.0f);

    const std::vector<terraingen::Vec2d> points{{3.0, 4.0}, {5.0, 6.0}};
    std::vector<float> values{std::numeric_limits<float>::quiet_NaN(), 1.0f};
    terraingen::SampleBlock block{terraingen::Span<const terraingen::Vec2d>(points),
                                  terraingen::Span<float>(values)};
    context.spacing = 1.0;
    layer.Apply(block, context);
    EXPECT_TRUE(std::isnan(values[0]));
    EXPECT_NE(values[1], 1.0f);
}
