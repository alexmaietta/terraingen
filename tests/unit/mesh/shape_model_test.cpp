// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/shape_model.h"

#include "support/mesh_measures.h"

#include "terraingen/mesh/polyhedra.h"
#include "terraingen/util/log.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::RadialShape;
using terraingen::TriangleMesh;
using terraingen::Vec3d;

namespace {

/// The radius of the ellipsoid of semi-axes 270, 147 and 104 m along a unit direction.
double Ellipsoid(const Vec3d& u) {
    const double x = u.x / 270.0;
    const double y = u.y / 147.0;
    const double z = u.z / 104.0;
    return 1.0 / std::sqrt(x * x + y * y + z * z);
}

/// The messages logged while `run` runs.
template <typename Run> std::vector<std::string> Logged(Run run) {
    std::vector<std::string> messages;
    terraingen::SetLogSink(
        [&](const terraingen::LogRecord& record) { messages.push_back(record.message); });
    run();
    terraingen::SetLogSink(nullptr);
    return messages;
}

}  // namespace

TEST(ShapeModelFromMesh, FindsTheRadiusOfAStarShapedMeshAlongEachLabel) {
    const TriangleMesh mesh =
        terraingen::Scaled(terraingen::testing::Icosphere(6), Vec3d{270.0, 147.0, 104.0});
    std::optional<RadialShape> found;
    EXPECT_TRUE(Logged([&] { found = terraingen::ShapeModelFromMesh(mesh, 24); }).empty());
    const RadialShape& shape = *found;
    EXPECT_EQ(shape.Q(), 24u);
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j <= 24; j += 3) {
            for (std::size_t i = 0; i <= 24; i += 3) {
                const Vec3d d = RadialShape::LabelDirection(24, i, j, face);
                EXPECT_NEAR(shape.At(d) / Ellipsoid(d), 1.0, 1e-3) << i << " " << j << " " << face;
                EXPECT_LE(shape.At(d), Ellipsoid(d) * (1.0 + 1e-12)) << "facets lie inside";
            }
        }
    }
}

TEST(ShapeModelFromMesh, KeepsTheOuterSurfaceOfAFoldedMeshAndWarns) {
    TriangleMesh mesh =
        terraingen::Scaled(terraingen::testing::Icosphere(3), Vec3d{100.0, 100.0, 100.0});
    const TriangleMesh inner =
        terraingen::Scaled(terraingen::testing::Icosphere(3), Vec3d{50.0, 50.0, 50.0});
    const auto offset = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), inner.positions.begin(), inner.positions.end());
    for (auto t : inner.triangles) {
        mesh.triangles.push_back({t[0] + offset, t[1] + offset, t[2] + offset});
    }
    const std::vector<std::string> messages = Logged([&] {
        const RadialShape shape = terraingen::ShapeModelFromMesh(mesh, 4);
        EXPECT_GT(shape.MinRadius(), 95.0);
        EXPECT_LE(shape.MaxRadius(), 100.0 + 1e-9);
    });
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages[0].find("not star-shaped"), std::string::npos) << messages[0];
}

TEST(ShapeModelFromMesh, RejectsMeshesThatDoNotEncloseTheOrigin) {
    TriangleMesh mesh =
        terraingen::Scaled(terraingen::testing::Icosphere(2), Vec3d{10.0, 10.0, 10.0});
    EXPECT_THROW(terraingen::ShapeModelFromMesh(mesh, 0), std::invalid_argument);
    EXPECT_THROW(terraingen::ShapeModelFromMesh(TriangleMesh{}, 4), std::invalid_argument);
    for (Vec3d& p : mesh.positions) {
        p.x += 100.0;
    }
    EXPECT_THROW(terraingen::ShapeModelFromMesh(mesh, 4), std::runtime_error);
}
