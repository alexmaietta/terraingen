// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/geometry/radial_shape.h"

#include "terraingen/geometry/surface.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::RadialShape;
using terraingen::Vec3d;

namespace {

/// The radius of an ellipsoid with semi-axes 300, 150 and 100 m along a unit direction.
double Ellipsoid(const Vec3d& u) {
    const double x = u.x / 300.0;
    const double y = u.y / 150.0;
    const double z = u.z / 100.0;
    return 1.0 / std::sqrt(x * x + y * y + z * z);
}

}  // namespace

TEST(IcqLabels, AreIndexedAndVisitedInFileOrder) {
    EXPECT_EQ(terraingen::IcqLabelIndex(2, 0, 0, 1), 0u);
    EXPECT_EQ(terraingen::IcqLabelIndex(2, 1, 0, 1), 1u);
    EXPECT_EQ(terraingen::IcqLabelIndex(2, 0, 1, 1), 3u);
    EXPECT_EQ(terraingen::IcqLabelIndex(2, 0, 0, 2), 9u);
    std::size_t visited = 0;
    terraingen::ForEachIcqLabel(
        2, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t index) {
            EXPECT_EQ(index, visited);
            EXPECT_EQ(index, terraingen::IcqLabelIndex(2, i, j, face));
            ++visited;
        });
    EXPECT_EQ(visited, 54u);
}

TEST(RadialShape, LabelsTheFormatSharesHaveTheSameDirection) {
    const std::size_t q = 6;
    for (std::size_t k = 0; k <= q; ++k) {
        EXPECT_EQ(RadialShape::LabelDirection(q, k, 0, 2), RadialShape::LabelDirection(q, k, q, 1));
        EXPECT_EQ(RadialShape::LabelDirection(q, q, k, 3), RadialShape::LabelDirection(q, 0, k, 2));
        EXPECT_EQ(RadialShape::LabelDirection(q, 0, k, 6),
                  RadialShape::LabelDirection(q, q - k, q, 3));
    }
    EXPECT_EQ(RadialShape::LabelDirection(q, 3, 3, 1), (Vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(RadialShape::LabelDirection(q, 3, 3, 5), (Vec3d{1.0, 0.0, 0.0}));
    EXPECT_THROW(RadialShape::LabelDirection(q, 0, 0, 0), std::invalid_argument);
    EXPECT_THROW(RadialShape::LabelDirection(0, 0, 0, 1), std::invalid_argument);
}

TEST(RadialShape, InterpolatesBetweenLabelsAndIsExactAtThem) {
    const RadialShape shape = RadialShape::Sample(32, Ellipsoid);
    EXPECT_EQ(shape.Q(), 32u);
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j <= 32; j += 7) {
            for (std::size_t i = 0; i <= 32; i += 5) {
                const Vec3d d = RadialShape::LabelDirection(32, i, j, face);
                EXPECT_NEAR(shape.At(d), Ellipsoid(d), 1e-9 * Ellipsoid(d));
            }
        }
    }
    double worst = 0.0;
    for (int k = 0; k < 2000; ++k) {
        const Vec3d u = terraingen::UnitVector(static_cast<double>(k % 97) * 3.7 - 180.0,
                                               static_cast<double>(k % 89) * 2.0 - 88.0);
        worst = std::fmax(worst, std::fabs(shape.At(u) / Ellipsoid(u) - 1.0));
        EXPECT_GE(shape.At(u), shape.MinRadius());
        EXPECT_LE(shape.At(u), shape.MaxRadius());
    }
    EXPECT_LT(worst, 4e-3) << "bilinear between labels";
    EXPECT_NEAR(shape.MinRadius(), 100.0, 1e-9);
    EXPECT_NEAR(shape.MaxRadius(), 300.0, 1e-9);
    EXPECT_EQ(RadialShape(shape.Q(), shape.Radii()), shape);
}

TEST(RadialShape, BoundsTheAreaPerSteradianOfItsSurface) {
    const RadialShape shape = RadialShape::Sample(24, [](const Vec3d& u) {
        return Ellipsoid(u) * (1.0 + 0.15 * std::sin(5.0 * u.x) * std::cos(4.0 * u.y + u.z));
    });
    const terraingen::Surface body =
        terraingen::Surface::ShapeModel(std::make_shared<const RadialShape>(shape));
    for (int k = 0; k < 5000; ++k) {
        const terraingen::Vec2d point{static_cast<double>(k % 113) * 3.17 - 179.0,
                                      static_cast<double>(k % 71) * 2.5 - 87.0};
        EXPECT_LE(body.AreaPerSteradian(point), shape.MaxAreaPerSteradian());
    }
}

TEST(RadialShape, RejectsBadRadii) {
    EXPECT_THROW(RadialShape::Sample(0, Ellipsoid), std::invalid_argument);
    EXPECT_THROW(RadialShape(2, std::vector<double>(10, 1.0)), std::invalid_argument);
    std::vector<double> radii(6 * 3 * 3, 1.0);
    EXPECT_NO_THROW(RadialShape(2, radii));
    radii[4] = -1.0;
    EXPECT_THROW(RadialShape(2, radii), std::invalid_argument);
    radii[4] = 1.0;
    radii[0] = 2.0;
    EXPECT_THROW(RadialShape(2, radii), std::invalid_argument) << "a shared corner differs";
}
