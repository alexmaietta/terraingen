#include "ernie/geometry/primitives.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using ernie::Axis;
using ernie::MeshData;

namespace {

constexpr double kPi = 3.14159265358979323846;

glm::dvec3 Point(const MeshData& mesh, std::uint32_t i) {
    return {mesh.points[3 * i], mesh.points[3 * i + 1], mesh.points[3 * i + 2]};
}

glm::dvec3 Normal(const MeshData& mesh, std::uint32_t i) {
    return {mesh.normals[3 * i], mesh.normals[3 * i + 1], mesh.normals[3 * i + 2]};
}

double Area(const MeshData& mesh) {
    double area = 0.0;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const glm::dvec3 a = Point(mesh, mesh.indices[t]);
        area += 0.5 * glm::length(glm::cross(Point(mesh, mesh.indices[t + 1]) - a,
                                             Point(mesh, mesh.indices[t + 2]) - a));
    }
    return area;
}

/// Volume by the divergence theorem: positive only when every triangle winds outward.
double SignedVolume(const MeshData& mesh) {
    double volume = 0.0;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        volume +=
            glm::dot(Point(mesh, mesh.indices[t]), glm::cross(Point(mesh, mesh.indices[t + 1]),
                                                              Point(mesh, mesh.indices[t + 2]))) /
            6.0;
    }
    return volume;
}

std::pair<glm::dvec3, glm::dvec3> Bounds(const MeshData& mesh) {
    glm::dvec3 low(1e30);
    glm::dvec3 high(-1e30);
    for (std::uint32_t i = 0; i < mesh.points.size() / 3; ++i) {
        low = glm::min(low, Point(mesh, i));
        high = glm::max(high, Point(mesh, i));
    }
    return {low, high};
}

/// Unit normals, and every non-degenerate triangle facing the same way as its vertex normals.
void ExpectConsistent(const MeshData& mesh) {
    ASSERT_EQ(mesh.points.size(), mesh.normals.size());
    ASSERT_EQ(mesh.indices.size() % 3, 0u);
    for (std::uint32_t i = 0; i < mesh.points.size() / 3; ++i) {
        ASSERT_NEAR(glm::length(Normal(mesh, i)), 1.0, 1e-5) << "vertex " << i;
    }
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const std::uint32_t a = mesh.indices[t];
        const std::uint32_t b = mesh.indices[t + 1];
        const std::uint32_t c = mesh.indices[t + 2];
        ASSERT_LT(std::max({a, b, c}), mesh.points.size() / 3);
        const glm::dvec3 face =
            glm::cross(Point(mesh, b) - Point(mesh, a), Point(mesh, c) - Point(mesh, a));
        if (glm::length(face) < 1e-9) {
            continue;
        }
        const glm::dvec3 normals = Normal(mesh, a) + Normal(mesh, b) + Normal(mesh, c);
        ASSERT_GT(glm::dot(face, normals), 0.0) << "triangle " << t / 3;
    }
}

}  // namespace

TEST(Primitives, BoxesAreClosedAndSized) {
    const MeshData box = ernie::BoxMesh({2.0f, 4.0f, 6.0f});
    ExpectConsistent(box);
    EXPECT_EQ(box.points.size() / 3, 24u) << "four corners per face for flat normals";
    EXPECT_EQ(box.indices.size() / 3, 12u);
    EXPECT_NEAR(Area(box), 2.0 * (8.0 + 24.0 + 12.0), 1e-4);
    EXPECT_NEAR(SignedVolume(box), 48.0, 1e-4);
    const auto [low, high] = Bounds(box);
    EXPECT_EQ(low, glm::dvec3(-1.0, -2.0, -3.0));
    EXPECT_EQ(high, glm::dvec3(1.0, 2.0, 3.0));
}

TEST(Primitives, SpheresApproachTheirAreaAndVolume) {
    const MeshData sphere = ernie::SphereMesh(2.0f, 96, 48);
    ExpectConsistent(sphere);
    for (std::uint32_t i = 0; i < sphere.points.size() / 3; ++i) {
        ASSERT_NEAR(glm::length(Point(sphere, i)), 2.0, 1e-5);
    }
    EXPECT_NEAR(Area(sphere), 4.0 * kPi * 4.0, 0.01 * 4.0 * kPi * 4.0);
    EXPECT_NEAR(SignedVolume(sphere), 4.0 / 3.0 * kPi * 8.0, 0.01 * 4.0 / 3.0 * kPi * 8.0);
}

TEST(Primitives, CylindersConesAndCapsulesAreClosed) {
    const double r = 1.5;
    const double h = 4.0;
    const MeshData cylinder = ernie::CylinderMesh(1.5f, 4.0f, Axis::Z, 128);
    ExpectConsistent(cylinder);
    EXPECT_NEAR(Area(cylinder), 2.0 * kPi * r * h + 2.0 * kPi * r * r, 0.01 * 50.0);
    EXPECT_NEAR(SignedVolume(cylinder), kPi * r * r * h, 0.01 * kPi * r * r * h);

    const MeshData cone = ernie::ConeMesh(1.5f, 4.0f, Axis::Z, 128);
    ExpectConsistent(cone);
    const double slant = std::sqrt(r * r + h * h);
    EXPECT_NEAR(Area(cone), kPi * r * slant + kPi * r * r, 0.01 * 30.0);
    EXPECT_NEAR(SignedVolume(cone), kPi * r * r * h / 3.0, 0.01 * kPi * r * r * h / 3.0);
    EXPECT_NEAR(Bounds(cone).second.z, 2.0, 1e-6) << "apex at +height / 2";

    const MeshData capsule = ernie::CapsuleMesh(1.5f, 4.0f, Axis::Z, 128, 64);
    ExpectConsistent(capsule);
    const double capsuleVolume = kPi * r * r * h + 4.0 / 3.0 * kPi * r * r * r;
    EXPECT_NEAR(Area(capsule), 2.0 * kPi * r * h + 4.0 * kPi * r * r, 0.01 * 70.0);
    EXPECT_NEAR(SignedVolume(capsule), capsuleVolume, 0.01 * capsuleVolume);
    EXPECT_NEAR(Bounds(capsule).second.z, 0.5 * h + r, 1e-6) << "caps beyond the height";
    EXPECT_NO_THROW(ernie::CapsuleMesh(1.0f, 0.0f)) << "a capsule of height 0 is a sphere";
}

TEST(Primitives, AxesTurnShapesWithoutMirroringThem) {
    for (const Axis axis : {Axis::X, Axis::Y}) {
        const MeshData cone = ernie::ConeMesh(1.0f, 4.0f, axis);
        ExpectConsistent(cone);
        EXPECT_GT(SignedVolume(cone), 0.0) << "still wound outward";
        const glm::dvec3 high = Bounds(cone).second;
        EXPECT_NEAR(axis == Axis::X ? high.x : high.y, 2.0, 1e-6) << "apex along the axis";
    }
    const MeshData x = ernie::PlaneMesh(2.0f, 6.0f, Axis::X);
    ExpectConsistent(x);
    EXPECT_EQ(Normal(x, 0), glm::dvec3(1.0, 0.0, 0.0));
    EXPECT_NEAR(Bounds(x).second.y, 3.0, 1e-6) << "length along y";
    EXPECT_NEAR(Bounds(x).second.z, 1.0, 1e-6) << "width along z";
    const MeshData y = ernie::PlaneMesh(2.0f, 6.0f, Axis::Y);
    EXPECT_EQ(Normal(y, 0), glm::dvec3(0.0, 1.0, 0.0));
    EXPECT_NEAR(Bounds(y).second.z, 3.0, 1e-6);
    const MeshData z = ernie::PlaneMesh(2.0f, 6.0f);
    ExpectConsistent(z);
    EXPECT_NEAR(Area(z), 12.0, 1e-5);

    EXPECT_EQ(ernie::AxisNamed("Y"), Axis::Y);
    EXPECT_FALSE(ernie::AxisNamed("W").has_value());
}

TEST(Primitives, RejectInvalidSizes) {
    EXPECT_THROW(ernie::BoxMesh({1.0f, 0.0f, 1.0f}), std::invalid_argument);
    EXPECT_THROW(ernie::SphereMesh(-1.0f), std::invalid_argument);
    EXPECT_THROW(ernie::SphereMesh(1.0f, 2), std::invalid_argument);
    EXPECT_THROW(ernie::CylinderMesh(1.0f, std::nanf("")), std::invalid_argument);
    EXPECT_THROW(ernie::CapsuleMesh(1.0f, -1.0f), std::invalid_argument);
    EXPECT_THROW(ernie::PlaneMesh(1.0f, 0.0f), std::invalid_argument);
}
