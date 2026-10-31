// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/ground.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace {

/// A plane z = 2x - 3y, with no data west of x = -50.
class Tilted final : public terraingen::Ground {
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const terraingen::Vec2d p = samples.Position(i);
            samples.Height(i) = p.x < -50.0 ? std::numeric_limits<float>::quiet_NaN()
                                            : static_cast<float>(2.0 * p.x - 3.0 * p.y);
        }
    }
};

}  // namespace

TEST(Ground, ReadsPointsThroughFillSamples) {
    const Tilted ground;
    EXPECT_EQ(ground.HeightAt(1.0, 1.0, 1.0), -1.0f);
    const std::array<terraingen::Vec2d, 2> points{terraingen::Vec2d{2.0, 0.0},
                                                  terraingen::Vec2d{-60.0, 0.0}};
    std::array<float, 2> heights{};
    ground.HeightsAt(points, heights, 1.0);
    EXPECT_EQ(heights[0], 4.0f);
    EXPECT_TRUE(std::isnan(heights[1]));
}

TEST(GradientPoints, StepEastWestNorthSouth) {
    const auto points = terraingen::GradientPoints(terraingen::Surface{}, {10.0, 20.0}, 2.0);
    EXPECT_EQ(points[0], (terraingen::Vec2d{12.0, 20.0}));
    EXPECT_EQ(points[1], (terraingen::Vec2d{8.0, 20.0}));
    EXPECT_EQ(points[2], (terraingen::Vec2d{10.0, 22.0}));
    EXPECT_EQ(points[3], (terraingen::Vec2d{10.0, 18.0}));
}

TEST(GroundGradient, ReadsCentralDifferences) {
    const Tilted ground;
    const auto gradient = terraingen::GroundGradient(ground, {}, {0.0, 0.0}, 1.5, 1.0);
    ASSERT_TRUE(gradient);
    EXPECT_NEAR(gradient->x, 2.0, 1e-6);
    EXPECT_NEAR(gradient->y, -3.0, 1e-6);
    EXPECT_FALSE(terraingen::GroundGradient(ground, {}, {-49.0, 0.0}, 2.0, 1.0))
        << "no gradient where the ground has no data";
    const std::array<float, 4> heights{3.0f, 1.0f, 0.5f, 0.5f};
    const auto fromHeights = terraingen::GradientFromHeights(heights, 0.5);
    ASSERT_TRUE(fromHeights);
    EXPECT_EQ(fromHeights->x, 2.0);
    EXPECT_EQ(fromHeights->y, 0.0);
}

TEST(GroundAround, ReadsOffsetsAndFallsBackWithoutData) {
    const Tilted ground;
    const std::array<terraingen::Vec2d, 3> offsets{
        terraingen::Vec2d{1.0, 0.0}, terraingen::Vec2d{0.0, 1.0}, terraingen::Vec2d{-100.0, 0.0}};
    std::array<double, 3> heights{};
    terraingen::GroundAround(ground, {}, {0.0, 0.0}, offsets, 1.0, 7.0, heights);
    EXPECT_EQ(heights[0], 2.0);
    EXPECT_EQ(heights[1], -3.0);
    EXPECT_EQ(heights[2], 7.0);
    const terraingen::Surface sphere = terraingen::Surface::Sphere(1000.0);
    terraingen::GroundAround(ground, sphere, {0.0, 0.0}, offsets, 1.0, 7.0, heights);
    const terraingen::Vec2d east = sphere.Locate({0.0, 0.0}, offsets[0]);
    EXPECT_EQ(heights[0], static_cast<double>(ground.HeightAt(east.x, east.y, 1.0)))
        << "offsets lie in the local frame";
}
