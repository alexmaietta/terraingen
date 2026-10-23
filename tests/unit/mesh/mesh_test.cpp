#include "terraingen/mesh/mesh.h"

#include "support/mesh_measures.h"

#include "support/mesh_checks.h"
#include "terraingen/mesh/polyhedra.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::RigidTransform;
using terraingen::TriangleMesh;
using terraingen::Vec3d;

namespace {

void ExpectNear(const Vec3d& a, const Vec3d& b, double tolerance = 1e-12) {
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

}  // namespace

TEST(TriangleMesh, MeasuresBoundsVolumeAndArea) {
    const TriangleMesh cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto box = terraingen::Bounds(cube);
    ExpectNear(box.min, {-1, -1, -1});
    ExpectNear(box.Extents(), {2, 2, 2});
    EXPECT_NEAR(terraingen::testing::Volume(cube), 8.0, 1e-12);
    EXPECT_NEAR(terraingen::testing::SurfaceArea(cube), 24.0, 1e-12);
    EXPECT_THROW(terraingen::Bounds(TriangleMesh{}), std::invalid_argument);
    TriangleMesh inward = cube;
    for (auto& t : inward.triangles) {
        std::swap(t[1], t[2]);
    }
    EXPECT_NEAR(terraingen::testing::Volume(inward), -8.0, 1e-12);
}

TEST(TriangleMesh, HasOutwardVertexNormals) {
    const TriangleMesh sphere = terraingen::testing::Icosphere(3);
    const auto normals = terraingen::VertexNormals(sphere);
    ASSERT_EQ(normals.size(), sphere.positions.size());
    for (std::size_t v = 0; v < normals.size(); ++v) {
        EXPECT_NEAR(terraingen::Length(normals[v]), 1.0, 1e-12);
        EXPECT_GT(terraingen::Dot(normals[v], sphere.positions[v]), 0.99);
    }
    EXPECT_EQ(terraingen::Normalized(Vec3d{}), Vec3d{});
}

TEST(TriangleMesh, SplitsVerticesAtCreases) {
    const TriangleMesh cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto hard = terraingen::WithCreases(cube, 0.5);
    EXPECT_EQ(hard.mesh.positions.size(), 24u) << "four corners per face";
    EXPECT_EQ(hard.mesh.triangles.size(), cube.triangles.size());
    for (std::size_t f = 0; f < hard.mesh.triangles.size(); ++f) {
        const auto& t = hard.mesh.triangles[f];
        const Vec3d face = terraingen::Normalized(
            terraingen::Cross(hard.mesh.positions[t[1]] - hard.mesh.positions[t[0]],
                              hard.mesh.positions[t[2]] - hard.mesh.positions[t[0]]));
        for (const std::uint32_t v : t) {
            ExpectNear(hard.normals[v], face);
        }
    }
    EXPECT_NEAR(terraingen::testing::Volume(hard.mesh), 8.0, 1e-12);
    const auto smooth = terraingen::WithCreases(cube, terraingen::kPi);
    EXPECT_EQ(smooth.mesh.positions.size(), 8u);
    EXPECT_THROW(terraingen::WithCreases(cube, -1.0), std::invalid_argument);
}

TEST(RigidTransform, RotatesTranslatesAndComposes) {
    const auto quarter = RigidTransform::RotationZ(terraingen::kPi / 2.0);
    ExpectNear(quarter.Apply({1, 0, 0}), {0, 1, 0});
    ExpectNear(RigidTransform::RotationX(terraingen::kPi / 2.0).Apply({0, 1, 0}), {0, 0, 1});
    ExpectNear(RigidTransform::RotationY(terraingen::kPi / 2.0).Apply({0, 0, 1}), {1, 0, 0});
    const auto moved = RigidTransform::Translation({1, 2, 3}).After(quarter);
    ExpectNear(moved.Apply({1, 0, 0}), {1, 3, 3});
    const auto both = quarter.After(RigidTransform::Translation({1, 0, 0}));
    ExpectNear(both.Apply({0, 0, 0}), {0, 1, 0});
    ExpectNear(moved.Rotate({1, 0, 0}), {0, 1, 0});
    const Vec3d p{0.3, -1.2, 2.5};
    const Vec3d applied = moved.Apply(p);
    const Vec3d rotated = moved.Rotate(p) + moved.translation;
    EXPECT_EQ(applied.x, rotated.x);
    EXPECT_EQ(applied.y, rotated.y);
    EXPECT_EQ(applied.z, rotated.z);
}

TEST(TriangleAreaNormal, PointsOutOfACounterClockwiseTriangleWithTwiceItsArea) {
    ExpectNear(terraingen::TriangleAreaNormal({0, 0, 0}, {2, 0, 0}, {0, 3, 0}), {0, 0, 6});
    ExpectNear(terraingen::TriangleAreaNormal({0, 0, 0}, {0, 3, 0}, {2, 0, 0}), {0, 0, -6});
    ExpectNear(terraingen::TriangleAreaNormal({1, 1, 1}, {2, 2, 2}, {3, 3, 3}), {0, 0, 0});
}

TEST(RequireScaleFactors, RejectsZeroAndNonFiniteFactorsByName) {
    EXPECT_NO_THROW(terraingen::RequireScaleFactors({-1.0, 2.0, 0.5}, "Scales"));
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInf = std::numeric_limits<double>::infinity();
    for (const Vec3d& bad : {Vec3d{0.0, 1.0, 1.0}, Vec3d{1.0, kNaN, 1.0}, Vec3d{1.0, 1.0, kInf}}) {
        try {
            terraingen::RequireScaleFactors(bad, "Node scales");
            ADD_FAILURE() << "accepted a bad scale";
        } catch (const std::invalid_argument& error) {
            EXPECT_STREQ(error.what(), "Node scales must be finite and not zero");
        }
    }
}

TEST(Scaled, ScalesPerAxisAndKeepsTheMeshOutward) {
    const TriangleMesh cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    EXPECT_NEAR(terraingen::testing::Volume(terraingen::Scaled(cube, {2, 3, 0.5})), 24.0, 1e-12);
    const TriangleMesh mirrored = terraingen::Scaled(cube, {-1, 1, 1});
    EXPECT_NEAR(terraingen::testing::Volume(mirrored), 8.0, 1e-12);
    EXPECT_TRUE(terraingen::testing::IsClosedAndOriented(mirrored));
    EXPECT_THROW(terraingen::Scaled(cube, {0, 1, 1}), std::invalid_argument);
}

TEST(RequireRotation, AcceptsRotationsAndRejectsReflectionsAndSkews) {
    EXPECT_NO_THROW(terraingen::RequireRotation(RigidTransform::RotationY(0.3).rotation));
    std::array<double, 9> mirror = RigidTransform{}.rotation;
    mirror[0] = -1.0;
    EXPECT_THROW(terraingen::RequireRotation(mirror), std::invalid_argument);
    std::array<double, 9> skew = RigidTransform{}.rotation;
    skew[1] = 0.5;
    EXPECT_THROW(terraingen::RequireRotation(skew), std::invalid_argument);
}

TEST(TriangleMesh, RejectsTrianglesIndexingPastTheVertices) {
    TriangleMesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};
    EXPECT_NO_THROW(terraingen::ValidateMesh(mesh));
    mesh.triangles.push_back({0, 1, 3});
    EXPECT_THROW(terraingen::ValidateMesh(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::testing::Volume(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::testing::SurfaceArea(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::VertexNormals(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::WithCreases(mesh, 0.5), std::invalid_argument);
}
