#include "terraingen/layers/boulders/boulder_meshes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using terraingen::Boulder;

TEST(PlaceBoulderModel, StandsTheShapeAtTheBouldersSizeBurialAndHeading) {
    const terraingen::BoulderShapeOptions options;
    const Boulder boulder{5.0, -3.0, 2.0, 1.0, 0.25, 0.4, 0.0, 42u};
    const auto shape = terraingen::GenerateBoulderShape(boulder.id, options);
    const auto mesh = terraingen::BoulderMesh(boulder, options, 10.0);
    const auto box = terraingen::Bounds(mesh);
    EXPECT_NEAR(box.min.z, 10.0 - 0.25, 1e-12) << "a quarter of its height below the ground";
    EXPECT_NEAR(box.max.z - 10.0, boulder.ExposedHeight(), 1e-12);
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
    EXPECT_NEAR(along - low, boulder.diameter, 1e-9) << "the long axis follows the yaw";

    const auto placement = terraingen::PlaceBoulderModel(boulder, shape, 10.0);
    EXPECT_EQ(placement.Apply(shape.mesh.positions[3]), mesh.positions[3]);
    EXPECT_EQ(placement.scale.x, 2.0);
}

TEST(PlaceBoulderModel, BuriesItsShareOfTheLeanedHeightAndSharesPrototypes) {
    const terraingen::BoulderShapeOptions options;
    const Boulder boulder{0.0, 0.0, 1.0, 0.5, 0.4, 0.0, std::numbers::pi / 6.0, 7u};
    const auto box = terraingen::Bounds(terraingen::BoulderMesh(boulder, options, 0.0));
    const double extent = box.max.z - box.min.z;
    EXPECT_GT(extent, boulder.height) << "leaning raises the long axis";
    EXPECT_NEAR(box.min.z, -0.4 * extent, 1e-12);

    Boulder other = boulder;
    other.id = 8u;
    const auto a = terraingen::BoulderMesh(boulder, options, 0.0, 1);
    const auto b = terraingen::BoulderMesh(other, options, 0.0, 1);
    EXPECT_EQ(a.positions, b.positions) << "one prototype for every boulder";
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

TEST(PlacementAtSite, MovesLocalPlacementsIntoTheSitesFrame) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    terraingen::BoulderPlacement local;
    local.scale = {2.0, 1.5, 1.0};
    local.pose = terraingen::RigidTransform::RotationZ(0.7);
    local.pose.translation = {0.0, 0.0, 3.0};
    const terraingen::Vec2d site{20.0, 10.0};
    const auto same = terraingen::PlacementAtSite(local, site, site, moon);
    for (std::size_t i = 0; i < 9; ++i) {
        EXPECT_NEAR(same.pose.rotation[i], local.pose.rotation[i], 1e-12);
    }
    EXPECT_NEAR(same.pose.translation.z, 3.0, 1e-9);
    EXPECT_NEAR(same.pose.translation.x, 0.0, 1e-9);

    const terraingen::Vec2d boulder = moon.Locate(site, {800.0, -300.0});
    const auto moved = terraingen::PlacementAtSite(local, boulder, site, moon);
    const terraingen::Vec3d q{0.4, -0.2, 0.5};
    const terraingen::Vec3d atSite = moved.Apply(q);
    const terraingen::Vec3d inBoulder = local.Apply(q);
    terraingen::LocalFrame frame = moon.Frame(boulder);
    terraingen::Vec3d e = frame.east;
    terraingen::Vec3d n = frame.north;
    terraingen::Vec3d u = frame.up;
    const terraingen::Vec3d pr = moon.Position(boulder);
    const terraingen::Vec3d body{pr.x + e.x * inBoulder.x + n.x * inBoulder.y + u.x * inBoulder.z,
                                 pr.y + e.y * inBoulder.x + n.y * inBoulder.y + u.y * inBoulder.z,
                                 pr.z + e.z * inBoulder.x + n.z * inBoulder.y + u.z * inBoulder.z};
    frame = moon.Frame(site);
    e = frame.east;
    n = frame.north;
    u = frame.up;
    const terraingen::Vec3d ps = moon.Position(site);
    const terraingen::Vec3d back{ps.x + e.x * atSite.x + n.x * atSite.y + u.x * atSite.z,
                                 ps.y + e.y * atSite.x + n.y * atSite.y + u.y * atSite.z,
                                 ps.z + e.z * atSite.x + n.z * atSite.y + u.z * atSite.z};
    EXPECT_NEAR(back.x, body.x, 1e-6);
    EXPECT_NEAR(back.y, body.y, 1e-6);
    EXPECT_NEAR(back.z, body.z, 1e-6);
    EXPECT_NEAR(atSite.x, 800.0, 1.0);
    EXPECT_NEAR(atSite.y, -300.0, 1.0);
    EXPECT_LT(atSite.z, 3.5);
    const auto flat = terraingen::PlacementAtSite(local, {5.0, 5.0}, {0.0, 0.0}, {});
    EXPECT_EQ(flat.pose.translation, local.pose.translation);
}
