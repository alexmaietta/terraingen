#include "terraingen/layers/rocks/boulder_meshes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using terraingen::Rock;

TEST(PlaceBoulder, StandsTheShapeAtTheRocksSizeBurialAndHeading) {
    const terraingen::BoulderShapeOptions options;
    const Rock rock{5.0, -3.0, 2.0, 1.0, 0.25, 0.4, 0.0, 42u};
    const auto shape = terraingen::GenerateBoulderShape(rock.id, options);
    const auto mesh = terraingen::BoulderMesh(rock, options, 10.0);
    const auto box = terraingen::Bounds(mesh);
    EXPECT_NEAR(box.min.z, 10.0 - 0.25, 1e-12) << "a quarter of its height below the ground";
    EXPECT_NEAR(box.max.z - 10.0, rock.ExposedHeight(), 1e-12);
    EXPECT_NEAR(terraingen::Volume(mesh),
                terraingen::Volume(shape.mesh) * 2.0 * 2.0 * (1.0 / shape.shortAxis), 1e-9);
    double along = 0.0;
    const terraingen::Vec3d heading{std::cos(0.4), std::sin(0.4), 0.0};
    double low = 1e9;
    for (const auto& p : mesh.positions) {
        const double t = terraingen::Dot(p - terraingen::Vec3d{5.0, -3.0, 0.0}, heading);
        along = std::fmax(along, t);
        low = std::fmin(low, t);
    }
    EXPECT_NEAR(along - low, rock.diameter, 1e-9) << "the long axis follows the yaw";

    const auto placement = terraingen::PlaceBoulder(rock, shape, 10.0);
    EXPECT_EQ(placement.Apply(shape.mesh.positions[3]), mesh.positions[3]);
    EXPECT_EQ(placement.scale.x, 2.0);
}

TEST(PlaceBoulder, BuriesItsShareOfTheLeanedHeightAndSharesPrototypes) {
    const terraingen::BoulderShapeOptions options;
    const Rock rock{0.0, 0.0, 1.0, 0.5, 0.4, 0.0, std::numbers::pi / 6.0, 7u};
    const auto box = terraingen::Bounds(terraingen::BoulderMesh(rock, options, 0.0));
    const double extent = box.max.z - box.min.z;
    EXPECT_GT(extent, rock.height) << "leaning raises the long axis";
    EXPECT_NEAR(box.min.z, -0.4 * extent, 1e-12);

    Rock other = rock;
    other.id = 8u;
    const auto a = terraingen::BoulderMesh(rock, options, 0.0, 1);
    const auto b = terraingen::BoulderMesh(other, options, 0.0, 1);
    EXPECT_EQ(a.positions, b.positions) << "one prototype for every rock";
}

TEST(TopSurface, IsTheHighestUpwardFaceAboveAPoint) {
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto top = terraingen::TopSurface(cube, 0.3, -0.2);
    ASSERT_TRUE(top.has_value());
    EXPECT_DOUBLE_EQ(*top, 1.0);
    EXPECT_FALSE(terraingen::TopSurface(cube, 1.5, 0.0).has_value());
    const auto pyramid = terraingen::BaseSolidMesh(terraingen::BaseSolid::Pyramid);
    EXPECT_NEAR(*terraingen::TopSurface(pyramid, 0.5, 0.0), 0.0, 1e-12) << "halfway down a side";
}
