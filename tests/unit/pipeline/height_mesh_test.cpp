#include "terraingen/pipeline/height_mesh.h"

#include "terraingen/raster/geo_transform.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;

TEST(HeightMesh, PutsAVertexAtEveryCellWithDataAndFacesUp) {
    Raster<float> heights(4, 3, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(100.0, 50.0, 2.0));
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            heights(col, row) = static_cast<float>(col + 10 * row);
        }
    }
    heights.SetNoData(-9999.0f);
    heights(3, 2) = -9999.0f;
    const auto mesh = terraingen::HeightMesh(heights);
    ASSERT_EQ(mesh.positions.size(), 11u);
    EXPECT_EQ(mesh.triangles.size(), 2u * 6u - 2u) << "no square through the hole";
    EXPECT_EQ(mesh.positions[5].x, 103.0);
    EXPECT_EQ(mesh.positions[5].y, 47.0);
    EXPECT_EQ(mesh.positions[5].z, 11.0);
    for (const auto& t : mesh.triangles) {
        const auto& a = mesh.positions[t[0]];
        const auto& b = mesh.positions[t[1]];
        const auto& c = mesh.positions[t[2]];
        EXPECT_GT((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x), 0.0) << "facing up";
    }
    Raster<float> one(1, 1, 0.0f);
    EXPECT_THROW(terraingen::HeightMesh(one), std::invalid_argument);
}

TEST(HeightMesh, IsInTheSiteFrameOnASphere) {
    const auto moon = terraingen::Surface::Sphere(1737400.0);
    const double step = 10.0 / moon.UnitLength();
    Raster<float> heights(3, 3, 5.0f);
    heights.SetTransform(GeoTransform::NorthUp(20.0 - 1.5 * step, 40.0 + 1.5 * step, step));
    const auto mesh = terraingen::HeightMesh(heights, moon, {20.0, 40.0});
    const auto& centre = mesh.positions[4];
    EXPECT_NEAR(centre.x, 0.0, 1e-6);
    EXPECT_NEAR(centre.y, 0.0, 1e-6);
    EXPECT_NEAR(centre.z, 5.0, 1e-6) << "the height above the datum at the site";
    EXPECT_NEAR(mesh.positions[5].x, 10.0 * std::cos(40.0 * std::acos(-1.0) / 180.0), 1e-3)
        << "a cell east, its longitude step shortened by latitude";
    EXPECT_LT(mesh.positions[5].z, 5.0) << "the datum curves away";
}
