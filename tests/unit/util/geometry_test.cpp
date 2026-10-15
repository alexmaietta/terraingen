/// @file
/// @brief Tests of the points, vectors and boxes in util/geometry.h.
#include "terraingen/util/geometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

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
}

TEST(Geometry, ConvertsDegreesToRadians) {
    EXPECT_DOUBLE_EQ(180.0 * terraingen::kDegree, std::numbers::pi);
}

TEST(Geometry, BoundsAPointByTheNextRepresentableValues) {
    const terraingen::Bounds2d box = terraingen::PointBounds(1.5, -2.0);
    EXPECT_TRUE(box.Contains(1.5, -2.0));
    EXPECT_FALSE(box.Contains(std::nextafter(1.5, 2.0), -2.0));
    EXPECT_FALSE(box.Contains(1.5, std::nextafter(-2.0, 0.0)));
    EXPECT_EQ(box.minX, 1.5);
    EXPECT_EQ(box.maxY, std::nextafter(-2.0, 0.0));
}
