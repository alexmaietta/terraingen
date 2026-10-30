// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/terrain.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

using terraingen::EvaluationOptions;
using terraingen::GeoTransform;
using terraingen::LayerContext;
using terraingen::RasterGrid;
using terraingen::SampleBlock;
using terraingen::Terrain;
using terraingen::Vec2d;

namespace {

/// The wave `Wave` adds.
double WaveAt(double x, double y) {
    return std::sin(0.37 * x) * std::cos(0.21 * y);
}

/// Adds `WaveAt` to every sample and counts the blocks it is applied to.
class Wave final : public terraingen::Layer {
  public:
    mutable std::atomic<std::size_t> blocks{0};

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& /*context*/) const override {
        ++blocks;
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) =
                static_cast<float>(static_cast<double>(samples.Height(i)) + WaveAt(p.x, p.y));
        }
    }
};

/// Reads the ground on a ring of eight points 3 m around each sample, as sand fill does.
class Ring final : public terraingen::FormulaLayer {
  public:
    /// The height after the layer from `height` before it, reading the ground through `at`.
    template <typename At> static float After(float height, double x, double y, const At& at) {
        double sum = 0.0;
        for (int k = 0; k < 8; ++k) {
            const double angle = 0.785 * k;
            sum += static_cast<double>(at(x + 3.0 * std::cos(angle), y + 3.0 * std::sin(angle)));
        }
        return static_cast<float>(static_cast<double>(height) + 0.1 * sum / 8.0);
    }

  protected:
    float HeightAt(float height, double x, double y, const LayerContext& context) const override {
        return After(height, x, y, [&](double px, double py) {
            return context.ground->HeightAt(px, py, context.spacing);
        });
    }
};

/// The ground beneath `Ring` in `Stack`: the wave over a datum, read point by point.
float WaveGround(double x, double y) {
    return static_cast<float>(0.0 + WaveAt(x, y));
}

struct Stack {
    std::shared_ptr<Wave> wave = std::make_shared<Wave>();
    Terrain terrain{terraingen::Surface{}, nullptr, {wave, std::make_shared<Ring>()}};
};

}  // namespace

TEST(GroundReads, AGridReadsTheGroundBeneathALayerInBatchesToTheSameBits) {
    const Stack stack;
    const RasterGrid grid{30, 30, GeoTransform::NorthUp(-30.0, 30.0, 2.0)};
    const auto heights = stack.terrain.Evaluate(grid, EvaluationOptions{}.Spacing(2.0));
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d c = heights.CellCenter(col, row);
            ASSERT_EQ(heights(col, row), Ring::After(WaveGround(c.x, c.y), c.x, c.y, WaveGround))
                << "cell " << col << ", " << row;
        }
    }
    EXPECT_LT(stack.wave->blocks.load(), 100u)
        << "the 7200 ground reads go through the layers beneath in a few batches, not one by one";
}

TEST(GroundReads, PointsReadTheGroundBeneathALayerInBatchesToTheSameBits) {
    const Stack stack;
    std::vector<Vec2d> points;
    for (int i = 0; i < 1000; ++i) {
        points.push_back({0.37 * i - 150.0, 0.11 * (i % 97) - 5.0});
    }
    std::vector<float> heights(points.size());
    stack.terrain.HeightsAt(points, heights, 2.0, 1);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec2d p = points[i];
        ASSERT_EQ(heights[i], Ring::After(WaveGround(p.x, p.y), p.x, p.y, WaveGround))
            << "point " << i;
    }
    EXPECT_LT(stack.wave->blocks.load(), 100u);
    const auto grid = stack.terrain.Evaluate({4, 3, GeoTransform::NorthUp(10.0, 20.0, 2.0)},
                                             EvaluationOptions{}.Spacing(2.0));
    const Vec2d c = grid.CellCenter(2, 1);
    EXPECT_EQ(stack.terrain.HeightAt(c.x, c.y, 2.0), grid(2, 1))
        << "a point still equals the grid cell centered there";
}
