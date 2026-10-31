// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/geometry/geometry.h"

#include <gtest/gtest.h>

#include <cmath>

using terraingen::Vec2d;
using terraingen::Vec3d;

TEST(Geometry, ComparesPoints) {
    EXPECT_EQ((Vec2d{1.0, 2.0}), (Vec2d{1.0, 2.0}));
    EXPECT_NE((Vec2d{1.0, 2.0}), (Vec2d{2.0, 1.0}));
}

TEST(Geometry, DoesVectorArithmetic) {
    const Vec3d a{1.0, 2.0, 3.0};
    const Vec3d b{4.0, -5.0, 6.0};
    EXPECT_EQ(a + b, (Vec3d{5.0, -3.0, 9.0}));
    EXPECT_EQ(a - b, (Vec3d{-3.0, 7.0, -3.0}));
    EXPECT_EQ(a * 2.0, (Vec3d{2.0, 4.0, 6.0}));
    EXPECT_EQ(terraingen::Dot(a, b), 12.0);
    EXPECT_EQ(terraingen::Cross(Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}),
              (Vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(terraingen::Length(Vec3d{3.0, 4.0, 0.0}), 5.0);
    EXPECT_EQ(terraingen::Normalized(Vec3d{0.0, 0.0, 2.0}), (Vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(terraingen::Normalized(Vec3d{}), Vec3d{});
    EXPECT_EQ(-a, (Vec3d{-1.0, -2.0, -3.0}));
    EXPECT_EQ(a / 2.0, (Vec3d{0.5, 1.0, 1.5}));
}

TEST(Geometry, DoesPlanarVectorArithmetic) {
    const Vec2d a{1.0, 2.0};
    const Vec2d b{4.0, -5.0};
    EXPECT_EQ(a + b, (Vec2d{5.0, -3.0}));
    EXPECT_EQ(a - b, (Vec2d{-3.0, 7.0}));
    EXPECT_EQ(-a, (Vec2d{-1.0, -2.0}));
    EXPECT_EQ(a * 3.0, (Vec2d{3.0, 6.0}));
    EXPECT_EQ(a / 4.0, (Vec2d{0.25, 0.5}));
}

TEST(Geometry, ConvertsDegreesToRadians) {
    EXPECT_DOUBLE_EQ(180.0 * terraingen::kDegree, terraingen::kPi);
    EXPECT_DOUBLE_EQ(terraingen::kPi * terraingen::kRadian, 180.0);
    EXPECT_DOUBLE_EQ(terraingen::kDegree * terraingen::kRadian, 1.0);
}

TEST(Geometry, MeasuresSlopeInDegreesFromAGradient) {
    EXPECT_EQ(terraingen::GradientSlopeDegrees(0.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::GradientSlopeDegrees(1.0, 0.0), 45.0);
    EXPECT_DOUBLE_EQ(terraingen::GradientSlopeDegrees(0.0, -1.0), 45.0);
    EXPECT_DOUBLE_EQ(terraingen::GradientSlopeDegrees(0.6, 0.8), 45.0);
    EXPECT_NEAR(terraingen::GradientSlopeDegrees(std::sqrt(3.0), 0.0), 60.0, 1e-12);
    EXPECT_TRUE(std::isnan(terraingen::GradientSlopeDegrees(std::nan(""), 0.0)));
}

TEST(Geometry, BoundsAPointByTheNextRepresentableValues) {
    const terraingen::Bounds2d box = terraingen::PointBounds(1.5, -2.0);
    EXPECT_TRUE(box.Contains(1.5, -2.0));
    EXPECT_FALSE(box.Contains(std::nextafter(1.5, 2.0), -2.0));
    EXPECT_FALSE(box.Contains(1.5, std::nextafter(-2.0, 0.0)));
    EXPECT_EQ(box.minX, 1.5);
    EXPECT_EQ(box.maxY, std::nextafter(-2.0, 0.0));
}

TEST(Geometry, BoundsAreFiniteOnlyWithEveryBoundFinite) {
    EXPECT_TRUE((terraingen::Bounds2d{-1.0, -2.0, 3.0, 4.0}.IsFinite()));
    EXPECT_FALSE((terraingen::Bounds2d{-1.0, -2.0, INFINITY, 4.0}.IsFinite()));
    EXPECT_FALSE((terraingen::Bounds2d{-1.0, NAN, 3.0, 4.0}.IsFinite()));
}

TEST(Geometry, GivesTheCenterOfABox) {
    const terraingen::Bounds2d box{-2.0, 1.0, 4.0, 5.0};
    EXPECT_EQ(box.Center(), (Vec2d{1.0, 3.0}));
}
