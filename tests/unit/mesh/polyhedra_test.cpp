#include "terraingen/mesh/polyhedra.h"

#include "terraingen/util/bits.h"
#include "tests/unit/mesh/support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::BaseSolid;

TEST(BaseSolidMesh, GivesClosedOutwardSolidsAroundTheOrigin) {
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    const double edge = 2.0 / std::sqrt(phi * phi + 1.0);
    const struct {
        BaseSolid solid;
        std::size_t vertices;
        std::size_t triangles;
        double volume;
    } cases[] = {
        {BaseSolid::Icosahedron, 12, 20, 5.0 / 12.0 * (3.0 + std::sqrt(5.0)) * std::pow(edge, 3)},
        {BaseSolid::Cube, 8, 12, 8.0},
        {BaseSolid::Pyramid, 5, 6, 8.0 / 3.0},
        {BaseSolid::Prism, 6, 8, 4.0},
        {BaseSolid::Tetrahedron, 4, 4, 8.0 / 3.0},
    };
    for (const auto& c : cases) {
        const auto mesh = terraingen::BaseSolidMesh(c.solid);
        EXPECT_EQ(mesh.positions.size(), c.vertices);
        EXPECT_EQ(mesh.triangles.size(), c.triangles);
        EXPECT_NEAR(terraingen::Volume(mesh), c.volume, 1e-12);
        EXPECT_TRUE(terraingen::testing::IsClosedAndOriented(mesh));
        EXPECT_TRUE(terraingen::testing::FacesAwayFromOrigin(mesh));
    }
    EXPECT_THROW(terraingen::BaseSolidMesh(static_cast<BaseSolid>(42)), std::invalid_argument);
}

TEST(Subdivide, SplitsEachTriangleInFourKeepingTheSolidAndItsVertices) {
    for (const BaseSolid solid :
         {BaseSolid::Cube, BaseSolid::Pyramid, BaseSolid::Prism, BaseSolid::Tetrahedron}) {
        const auto base = terraingen::BaseSolidMesh(solid);
        const auto once = terraingen::Subdivide(base, 1);
        const auto twice = terraingen::Subdivide(base, 2);
        EXPECT_EQ(twice.triangles.size(), 16 * base.triangles.size());
        const auto edges = 3 * twice.triangles.size() / 2;
        EXPECT_EQ(twice.positions.size() + twice.triangles.size(), edges + 2) << "Euler";
        EXPECT_NEAR(terraingen::Volume(twice), terraingen::Volume(base), 1e-12)
            << "planar faces stay planar";
        EXPECT_TRUE(terraingen::testing::IsClosedAndOriented(twice));
        for (std::size_t v = 0; v < once.positions.size(); ++v) {
            EXPECT_EQ(twice.positions[v], once.positions[v]) << "coarser vertices come first";
        }
    }
    EXPECT_THROW(terraingen::Subdivide(terraingen::BaseSolidMesh(BaseSolid::Cube), -1),
                 std::invalid_argument);
}

TEST(Icosphere, IsAGeodesicSphereOfUnitRadius) {
    for (int levels = 0; levels <= 4; ++levels) {
        const auto sphere = terraingen::Icosphere(levels);
        const std::size_t faces = 20u << (2 * levels);
        EXPECT_EQ(sphere.triangles.size(), faces);
        EXPECT_EQ(sphere.positions.size(), faces / 2 + 2);
        for (const auto& p : sphere.positions) {
            EXPECT_NEAR(terraingen::Length(p), 1.0, 1e-12);
        }
        EXPECT_TRUE(terraingen::testing::IsClosedAndOriented(sphere));
    }
    EXPECT_NEAR(terraingen::Volume(terraingen::Icosphere(5)), 4.0 / 3.0 * terraingen::kPi, 0.01);
    EXPECT_NEAR(terraingen::SurfaceArea(terraingen::Icosphere(5)), 4.0 * terraingen::kPi, 0.02);
    EXPECT_THROW(terraingen::Icosphere(-1), std::invalid_argument);
    EXPECT_THROW(terraingen::Icosphere(13), std::invalid_argument);
}
