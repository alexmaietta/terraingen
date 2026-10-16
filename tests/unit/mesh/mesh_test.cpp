#include "terraingen/mesh/mesh.h"

#include "terraingen/mesh/polyhedra.h"
#include "tests/unit/mesh/support.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
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
    EXPECT_NEAR(terraingen::Volume(cube), 8.0, 1e-12);
    EXPECT_NEAR(terraingen::SurfaceArea(cube), 24.0, 1e-12);
    EXPECT_THROW(terraingen::Bounds(TriangleMesh{}), std::invalid_argument);
    TriangleMesh inward = cube;
    for (auto& t : inward.triangles) {
        std::swap(t[1], t[2]);
    }
    EXPECT_NEAR(terraingen::Volume(inward), -8.0, 1e-12);
}

TEST(TriangleMesh, HasOutwardVertexNormals) {
    const TriangleMesh sphere = terraingen::Icosphere(3);
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
    EXPECT_NEAR(terraingen::Volume(hard.mesh), 8.0, 1e-12);
    const auto smooth = terraingen::WithCreases(cube, std::numbers::pi);
    EXPECT_EQ(smooth.mesh.positions.size(), 8u);
    EXPECT_THROW(terraingen::WithCreases(cube, -1.0), std::invalid_argument);
}

TEST(RigidTransform, RotatesTranslatesAndComposes) {
    const auto quarter = RigidTransform::RotationZ(std::numbers::pi / 2.0);
    ExpectNear(quarter.Apply({1, 0, 0}), {0, 1, 0});
    ExpectNear(RigidTransform::RotationX(std::numbers::pi / 2.0).Apply({0, 1, 0}), {0, 0, 1});
    ExpectNear(RigidTransform::RotationY(std::numbers::pi / 2.0).Apply({0, 0, 1}), {1, 0, 0});
    const auto moved = RigidTransform::Translation({1, 2, 3}).After(quarter);
    ExpectNear(moved.Apply({1, 0, 0}), {1, 3, 3});
    const auto both = quarter.After(RigidTransform::Translation({1, 0, 0}));
    ExpectNear(both.Apply({0, 0, 0}), {0, 1, 0});
    const TriangleMesh cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    EXPECT_NEAR(terraingen::Volume(terraingen::Transformed(cube, moved)), 8.0, 1e-9);
}

TEST(Scaled, ScalesPerAxisAndKeepsTheMeshOutward) {
    const TriangleMesh cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    EXPECT_NEAR(terraingen::Volume(terraingen::Scaled(cube, {2, 3, 0.5})), 24.0, 1e-12);
    const TriangleMesh mirrored = terraingen::Scaled(cube, {-1, 1, 1});
    EXPECT_NEAR(terraingen::Volume(mirrored), 8.0, 1e-12);
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

TEST(RotationQuaternion, CoversEveryBranchOfShepperdsMethod) {
    const auto expect = [](const std::array<double, 4>& q, const std::array<double, 4>& want) {
        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(std::fabs(q[i]), std::fabs(want[i]), 1e-12) << i;
        }
    };
    const double h = std::sqrt(0.5);
    expect(terraingen::RotationQuaternion(RigidTransform{}.rotation), {0, 0, 0, 1});
    expect(terraingen::RotationQuaternion(RigidTransform::RotationX(std::numbers::pi).rotation),
           {1, 0, 0, 0});
    expect(terraingen::RotationQuaternion(RigidTransform::RotationY(std::numbers::pi).rotation),
           {0, 1, 0, 0});
    expect(terraingen::RotationQuaternion(RigidTransform::RotationZ(std::numbers::pi).rotation),
           {0, 0, 1, 0});
    expect(terraingen::RotationQuaternion(RigidTransform::RotationZ(std::numbers::pi / 2).rotation),
           {0, 0, h, h});
}

TEST(TriangleMesh, RejectsTrianglesIndexingPastTheVertices) {
    TriangleMesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};
    EXPECT_NO_THROW(terraingen::ValidateMesh(mesh));
    mesh.triangles.push_back({0, 1, 3});
    EXPECT_THROW(terraingen::ValidateMesh(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::Volume(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::SurfaceArea(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::VertexNormals(mesh), std::invalid_argument);
    EXPECT_THROW(terraingen::WithCreases(mesh, 0.5), std::invalid_argument);
}
