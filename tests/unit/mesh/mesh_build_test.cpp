// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/mesh_build.h"

#include "terraingen/raster/geo_transform.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraingen::BuildMeshOptions;
using terraingen::GeoTransform;
using terraingen::MeshSize;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::Surface;
using terraingen::Vec2d;

namespace {

/// A grid of heights, the surface and site it is meshed in, and the options.
struct MeshCase {
    std::string name;
    Raster<float> heights;
    Surface surface;
    Vec2d site;
    BuildMeshOptions options;
};

/// A `width` x `height` grid of 1 m cells with varied heights.
Raster<float> Bumpy(std::size_t width, std::size_t height) {
    Raster<float> heights(
        RasterGrid{width, height, GeoTransform::NorthUp(0.0, static_cast<double>(height), 1.0)},
        0.0f);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            heights(col, row) =
                std::sin(0.7f * static_cast<float>(col)) + 0.3f * static_cast<float>(row * col % 5);
        }
    }
    return heights;
}

/// Plain grids, no-data holes, cutouts and skirts, alone and together, on a plane and a sphere.
std::vector<MeshCase> MeshCases() {
    std::vector<MeshCase> cases;
    cases.push_back({"plain", Bumpy(7, 5), Surface{}, Vec2d{}, BuildMeshOptions{}});

    Raster<float> holed = Bumpy(8, 6);
    holed.SetNoData(-9999.0f);
    holed(3, 2) = -9999.0f;
    holed(0, 0) = -9999.0f;
    holed(7, 5) = -9999.0f;
    cases.push_back({"holes", holed, Surface{}, Vec2d{}, BuildMeshOptions{}});

    BuildMeshOptions cut;
    cut.cutouts = {{1.5, 1.5, 3.5, 3.5}, {5.2, -1.0, 9.0, 1.8}};
    cases.push_back({"cutouts", Bumpy(8, 6), Surface{}, Vec2d{}, cut});

    BuildMeshOptions skirt;
    skirt.skirt = 2.5;
    cases.push_back({"skirt", Bumpy(6, 4), Surface{}, Vec2d{}, skirt});

    BuildMeshOptions all = cut;
    all.skirt = 1.25;
    cases.push_back({"holes, cutouts and skirt", holed, Surface{}, Vec2d{}, all});

    const Surface moon = Surface::Sphere(1737400.0);
    const double step = 10.0 / moon.UnitLength();
    Raster<float> bumpy = Bumpy(6, 5);
    Raster<float> spherical(RasterGrid{6, 5, GeoTransform::NorthUp(20.0, 40.0, step)},
                            bumpy.Values());
    spherical.SetNoData(-9999.0f);
    spherical(2, 2) = -9999.0f;
    BuildMeshOptions skirted;
    skirted.skirt = 3.0;
    cases.push_back(
        {"sphere", spherical, moon, Vec2d{20.0 + 2.0 * step, 40.0 - 2.0 * step}, skirted});
    return cases;
}

}  // namespace

TEST(BuildMesh, PutsAVertexAtEveryCellWithDataAndFacesUp) {
    Raster<float> heights(RasterGrid{4, 3, GeoTransform::NorthUp(100.0, 50.0, 2.0)}, 0.0f);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            heights(col, row) = static_cast<float>(col + 10 * row);
        }
    }
    heights.SetNoData(-9999.0f);
    heights(3, 2) = -9999.0f;
    const auto mesh = terraingen::BuildMesh(heights);
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
    EXPECT_THROW(terraingen::BuildMesh(one), std::invalid_argument);
}

TEST(BuildMesh, IsInTheSiteFrameOnASphere) {
    const auto moon = terraingen::Surface::Sphere(1737400.0);
    const double step = 10.0 / moon.UnitLength();
    Raster<float> heights(
        RasterGrid{3, 3, GeoTransform::NorthUp(20.0 - 1.5 * step, 40.0 + 1.5 * step, step)}, 5.0f);
    const auto mesh = terraingen::BuildMesh(heights, moon, {20.0, 40.0});
    const auto& center = mesh.positions[4];
    EXPECT_NEAR(center.x, 0.0, 1e-6);
    EXPECT_NEAR(center.y, 0.0, 1e-6);
    EXPECT_NEAR(center.z, 5.0, 1e-6) << "the height above the datum at the site";
    EXPECT_NEAR(mesh.positions[5].x, 10.0 * std::cos(40.0 * std::acos(-1.0) / 180.0), 1e-3)
        << "a cell east, its longitude step shortened by latitude";
    EXPECT_LT(mesh.positions[5].z, 5.0) << "the datum curves away";
}

TEST(BuildMesh, LeavesOutSquaresInCutoutsAndHangsAClosedSkirtBelowEveryEdge) {
    Raster<float> heights(RasterGrid{6, 6, GeoTransform::NorthUp(0.0, 6.0, 1.0)}, 1.0f);
    BuildMeshOptions options;
    options.cutouts = {{1.5, 1.5, 3.5, 3.5}};
    const auto holed = terraingen::BuildMesh(heights, {}, {}, options);
    EXPECT_EQ(holed.triangles.size(), 2u * (25u - 4u)) << "the four squares inside the box";

    options.skirt = 2.0;
    const auto skirted = terraingen::BuildMesh(heights, {}, {}, options);
    const std::size_t boundary = 4u * 5u + 4u * 2u;
    EXPECT_EQ(skirted.triangles.size(), holed.triangles.size() + 2u * boundary);
    EXPECT_EQ(skirted.positions.size(), holed.positions.size() + 20u + 8u);
    for (std::size_t i = holed.positions.size(); i < skirted.positions.size(); ++i) {
        EXPECT_EQ(skirted.positions[i].z, -1.0);
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& t : skirted.triangles) {
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[{t[k], t[(k + 1) % 3]}];
        }
    }
    std::size_t open = 0;
    for (const auto& [edge, count] : edges) {
        EXPECT_EQ(count, 1);
        open += edges.count({edge.second, edge.first}) == 0 ? 1u : 0u;
    }
    EXPECT_EQ(open, boundary) << "only the skirts' lower edges stay open";

    options.skirt = -1.0;
    EXPECT_THROW(terraingen::BuildMesh(heights, {}, {}, options), std::invalid_argument);
    options.skirt = 0.0;
    options.cutouts = {{-1.0, -1.0, 7.0, 7.0}};
    EXPECT_THROW(terraingen::BuildMesh(heights, {}, {}, options), std::invalid_argument);
}

TEST(GetMeshSize, CountsWhatBuildMeshMakes) {
    for (const MeshCase& c : MeshCases()) {
        const auto mesh = terraingen::BuildMesh(c.heights, c.surface, c.site, c.options);
        const MeshSize size = terraingen::GetMeshSize(c.heights, c.options);
        EXPECT_EQ(size.vertices, mesh.positions.size()) << c.name;
        EXPECT_EQ(size.triangles, mesh.triangles.size()) << c.name;
        EXPECT_EQ(size, (MeshSize{mesh.positions.size(), mesh.triangles.size()})) << c.name;
    }
    const MeshSize plain = terraingen::GetMeshSize(Bumpy(7, 5));
    EXPECT_EQ(plain, (MeshSize{35u, 2u * 6u * 4u}));
    EXPECT_NE(plain, (MeshSize{35u, 0u}));
}

TEST(GetMeshSize, RejectsWhatBuildMeshRejects) {
    EXPECT_THROW(terraingen::GetMeshSize(Raster<float>{1, 1, 0.0f}), std::invalid_argument);
    EXPECT_THROW(terraingen::GetMeshSize(Raster<float>{}), std::invalid_argument);
    BuildMeshOptions options;
    options.skirt = std::nan("");
    EXPECT_THROW(terraingen::GetMeshSize(Bumpy(3, 3), options), std::invalid_argument);
}

TEST(FillMesh, WritesWhatBuildMeshMakesToTheBit) {
    for (const MeshCase& c : MeshCases()) {
        const auto mesh = terraingen::BuildMesh(c.heights, c.surface, c.site, c.options);
        const MeshSize size = terraingen::GetMeshSize(c.heights, c.options);
        std::vector<double> positions(3 * size.vertices, -1.0);
        std::vector<std::int32_t> triangles(3 * size.triangles, -1);
        terraingen::FillMesh(c.heights, positions, triangles, c.surface, c.site, c.options);
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            const double expected[3] = {mesh.positions[i].x, mesh.positions[i].y,
                                        mesh.positions[i].z};
            ASSERT_EQ(std::memcmp(&positions[3 * i], expected, sizeof(expected)), 0)
                << c.name << " vertex " << i;
        }
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                ASSERT_EQ(triangles[3 * i + k], static_cast<std::int32_t>(mesh.triangles[i][k]))
                    << c.name << " triangle " << i;
            }
        }
    }
}

TEST(FillMesh, RejectsBuffersOfAnotherSize) {
    const Raster<float> heights = Bumpy(5, 4);
    BuildMeshOptions options;
    options.skirt = 1.0;
    const MeshSize size = terraingen::GetMeshSize(heights, options);
    std::vector<double> positions(3 * size.vertices);
    std::vector<std::int32_t> triangles(3 * size.triangles);
    EXPECT_NO_THROW(terraingen::FillMesh(heights, positions, triangles, {}, {}, options));
    std::vector<double> shortPositions(3 * size.vertices - 1);
    std::vector<double> longPositions(3 * size.vertices + 3);
    std::vector<std::int32_t> shortTriangles(3 * size.triangles - 3);
    std::vector<std::int32_t> longTriangles(3 * size.triangles + 1);
    EXPECT_THROW(terraingen::FillMesh(heights, shortPositions, triangles, {}, {}, options),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::FillMesh(heights, longPositions, triangles, {}, {}, options),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::FillMesh(heights, positions, shortTriangles, {}, {}, options),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::FillMesh(heights, positions, longTriangles, {}, {}, options),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::FillMesh(heights, positions, triangles), std::invalid_argument)
        << "sized for a skirt the call leaves out";
    EXPECT_THROW(terraingen::FillMesh(Raster<float>{1, 1, 0.0f}, {}, {}), std::invalid_argument);
}
