#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

using terraingen::Boulder;

TEST(PlaceMesh, MovesEveryPointByThePlacement) {
    terraingen::TriangleMesh mesh;
    mesh.positions = {{1.0, 0.0, 0.0}, {0.0, 1.0, -1.0}};
    terraingen::BoulderPlacement placement;
    placement.scale = {2.0, 3.0, 4.0};
    placement.pose.translation = {1.0, 1.0, 1.0};
    const terraingen::TriangleMesh placed = terraingen::PlaceMesh(mesh, placement);
    EXPECT_EQ(placed.positions[0].x, 3.0);
    EXPECT_EQ(placed.positions[1].y, 4.0);
    EXPECT_EQ(placed.positions[1].z, -3.0);
    const auto [low, high] = terraingen::ZRange(
        mesh.positions, [&](const terraingen::Vec3d& p) { return placement.Apply(p); });
    EXPECT_EQ(low, -3.0);
    EXPECT_EQ(high, 1.0);
}

TEST(ValueRange, LerpsFromMinToMax) {
    const terraingen::ValueRange range{2.0, 6.0};
    EXPECT_EQ(range.Lerp(0.0), 2.0);
    EXPECT_EQ(range.Lerp(0.25), 3.0);
}

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
    const Boulder boulder{0.0, 0.0, 1.0, 0.5, 0.4, 0.0, terraingen::kPi / 6.0, 7u};
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

namespace {

/// The centre of mass and placed points of `shape` placed as `boulder` on ground at 0.
std::pair<terraingen::Vec3d, std::vector<terraingen::Vec3d>>
Placed(const Boulder& boulder, const terraingen::BoulderShape& shape, bool settle) {
    const auto placement = terraingen::PlaceBoulderModel(boulder, shape, 0.0, settle);
    std::vector<terraingen::Vec3d> points;
    for (const auto& p : shape.mesh.positions) {
        points.push_back(placement.Apply(p));
    }
    terraingen::Vec3d sum{};
    double volume = 0.0;
    for (const auto& t : shape.mesh.triangles) {
        const double v =
            terraingen::Dot(points[t[0]], terraingen::Cross(points[t[1]], points[t[2]]));
        sum = sum + (points[t[0]] + points[t[1]] + points[t[2]]) * v;
        volume += v;
    }
    return {sum * (1.0 / (4.0 * volume)), points};
}

/// Whether the centre of mass stands over the points touching the ground: for every horizontal
/// direction some contact point reaches at least as far as the centre.
bool RestsStably(const terraingen::Vec3d& center, const std::vector<terraingen::Vec3d>& points) {
    double low = 1e300;
    for (const auto& p : points) {
        low = std::fmin(low, p.z);
    }
    for (int k = 0; k < 720; ++k) {
        const double a = 2.0 * terraingen::kPi * k / 720.0;
        const double ux = std::cos(a);
        const double uy = std::sin(a);
        double reach = -1e300;
        for (const auto& p : points) {
            if (p.z <= low + 1e-6) {
                reach = std::fmax(reach, p.x * ux + p.y * uy);
            }
        }
        if (reach < center.x * ux + center.y * uy - 1e-6) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(PlaceBoulderModel, SettlingLeavesEveryShapeRestingStablyAndNoHigher) {
    EXPECT_TRUE(terraingen::BoulderPose{}.settle) << "layers settle their models by default";
    const terraingen::BoulderShapeOptions options;
    int unsettledUnstable = 0;
    for (std::uint64_t id = 1; id <= 60; ++id) {
        Boulder boulder{0.0, 0.0, 1.5, 0.8, 0.0, 0.3 * static_cast<double>(id), 0.0, id};
        boulder.lean = 0.5 * std::sin(1.7 * static_cast<double>(id));
        boulder.rest = static_cast<terraingen::BoulderRest>(id % 3);
        const auto shape = terraingen::GenerateBoulderShape(id, options);
        const auto [before, drawn] = Placed(boulder, shape, false);
        const auto [after, settled] = Placed(boulder, shape, true);
        unsettledUnstable += RestsStably(before, drawn) ? 0 : 1;
        EXPECT_TRUE(RestsStably(after, settled)) << "shape " << id;
        double low = 1e300;
        double drawnLow = 1e300;
        for (std::size_t i = 0; i < settled.size(); ++i) {
            low = std::fmin(low, settled[i].z);
            drawnLow = std::fmin(drawnLow, drawn[i].z);
        }
        EXPECT_NEAR(low, 0.0, 1e-9) << "it stands on the ground, unburied";
        EXPECT_LE(after.z - low, before.z - drawnLow + 1e-9)
            << "tipping only lowers it, shape " << id;
    }
    EXPECT_GT(unsettledUnstable, 10) << "drawn poses often balance on an edge or corner";
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
