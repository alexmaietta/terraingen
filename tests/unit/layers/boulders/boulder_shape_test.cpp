#include "terraingen/layers/boulders/boulder_shape.h"

#include "tests/unit/mesh/support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using terraingen::BoulderShapeOptions;
using terraingen::BoulderStyle;
using terraingen::GenerateBoulderShape;

namespace {

/// The largest angle (radians) between the normals of two triangles sharing an edge of `mesh`.
double SharpestEdge(const terraingen::TriangleMesh& mesh) {
    std::map<std::pair<std::size_t, std::size_t>, std::vector<terraingen::Vec3d>> edges;
    for (const auto& t : mesh.triangles) {
        const auto& p = mesh.positions;
        const terraingen::Vec3d normal =
            terraingen::Normalized(terraingen::Cross(p[t[1]] - p[t[0]], p[t[2]] - p[t[0]]));
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t u = t[k];
            const std::size_t v = t[(k + 1) % 3];
            edges[{std::min(u, v), std::max(u, v)}].push_back(normal);
        }
    }
    double sharpest = 0.0;
    for (const auto& [edge, normals] : edges) {
        if (normals.size() == 2) {
            const double dot = std::clamp(terraingen::Dot(normals[0], normals[1]), -1.0, 1.0);
            sharpest = std::fmax(sharpest, std::acos(dot));
        }
    }
    return sharpest;
}

}  // namespace

TEST(GenerateBoulderShape, MakesClosedUnfoldedShapesOfTheirExactExtents) {
    const BoulderShapeOptions options;
    std::set<std::size_t> styles;
    for (std::uint64_t seed = 0; seed < 120; ++seed) {
        const auto shape = GenerateBoulderShape(seed, options);
        styles.insert(shape.style);
        const auto extents = terraingen::Bounds(shape.mesh).Extents();
        EXPECT_NEAR(extents.x, 1.0, 1e-12) << seed;
        EXPECT_NEAR(extents.y, shape.intermediate, 1e-12) << seed;
        EXPECT_NEAR(extents.z, shape.shortAxis, 1e-12) << seed;
        EXPECT_LE(shape.shortAxis, shape.intermediate);
        EXPECT_TRUE(terraingen::testing::IsClosedAndOriented(shape.mesh)) << seed;
        EXPECT_TRUE(terraingen::testing::FacesAwayFromOrigin(shape.mesh)) << seed;
        EXPECT_GT(terraingen::Volume(shape.mesh), 0.0);
    }
    EXPECT_EQ(styles.size(), options.styles.size()) << "every default style appears";
}

TEST(GenerateBoulderShape, IsAPureFunctionOfItsSeedAndOptions) {
    BoulderShapeOptions options;
    const auto a = GenerateBoulderShape(77, options);
    const auto b = GenerateBoulderShape(77, options);
    EXPECT_EQ(a.mesh.positions, b.mesh.positions);
    EXPECT_EQ(a.mesh.triangles, b.mesh.triangles);
    EXPECT_NE(GenerateBoulderShape(78, options).mesh.positions, a.mesh.positions);
    options.seed = 1;
    EXPECT_NE(GenerateBoulderShape(77, options).mesh.positions, a.mesh.positions);
}

TEST(GenerateBoulderShape, KeepsItsShapeAcrossLevelsOfDetail) {
    BoulderShapeOptions coarse;
    coarse.subdivisions = 2;
    BoulderShapeOptions fine;
    fine.subdivisions = 4;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const auto low = GenerateBoulderShape(seed, coarse);
        const auto high = GenerateBoulderShape(seed, fine);
        ASSERT_EQ(low.style, high.style);
        for (std::size_t v = 0; v < low.mesh.positions.size(); ++v) {
            EXPECT_LT(terraingen::Length(low.mesh.positions[v] - high.mesh.positions[v]), 0.05)
                << seed << " " << v;
        }
    }
}

TEST(GenerateBoulderShape, FollowsTheStyleWeightsAndSettings) {
    BoulderShapeOptions options;
    options.styles = {BoulderStyle{.name = "slab",
                                   .weight = 3.0,
                                   .solid = terraingen::BaseSolid::Cube,
                                   .shortAxis = {0.2, 0.2}},
                      BoulderStyle{.name = "ball",
                                   .weight = 1.0,
                                   .solid = terraingen::BaseSolid::Icosahedron,
                                   .intermediate = {0.9, 0.9},
                                   .shortAxis = {0.8, 0.8},
                                   .noiseAmplitude = 0.0},
                      BoulderStyle{.name = "never", .weight = 0.0}};
    options.subdivisions = 1;
    int slabs = 0;
    constexpr int kCount = 2000;
    for (std::uint64_t seed = 0; seed < kCount; ++seed) {
        const auto shape = GenerateBoulderShape(seed, options);
        ASSERT_NE(shape.style, 2u);
        if (shape.style == 0) {
            ++slabs;
            EXPECT_DOUBLE_EQ(shape.shortAxis, 0.2);
        }
    }
    EXPECT_NEAR(slabs / static_cast<double>(kCount), 0.75, 0.04);
    options.styles = {options.styles[1]};
    options.subdivisions = 4;
    const auto ball = GenerateBoulderShape(1, options);
    EXPECT_NEAR(terraingen::Volume(ball.mesh), std::acos(-1.0) / 6.0 * 0.9 * 0.8, 0.01)
        << "a smooth ellipsoid of axes 1, 0.9 and 0.8";
}

TEST(GenerateBoulderShape, CutsFlatFractureFaces) {
    BoulderShapeOptions options;
    options.styles = {BoulderStyle{.name = "cut",
                                   .solid = terraingen::BaseSolid::Icosahedron,
                                   .minFractures = 1,
                                   .maxFractures = 1,
                                   .fractureDepth = {0.5, 0.5},
                                   .noiseAmplitude = 0.0}};
    const auto cut = GenerateBoulderShape(3, options);
    options.styles[0].maxFractures = options.styles[0].minFractures = 0;
    const auto whole = GenerateBoulderShape(3, options);
    EXPECT_LT(terraingen::Volume(cut.mesh) / (cut.intermediate * cut.shortAxis),
              0.95 * terraingen::Volume(whole.mesh) / (whole.intermediate * whole.shortAxis));
}

TEST(ValidateBoulderShapeOptions, RejectsInvalidStyles) {
    EXPECT_NO_THROW(terraingen::ValidateBoulderShapeOptions({}));
    const std::vector<std::function<void(BoulderShapeOptions&)>> changes{
        [](BoulderShapeOptions& o) { o.styles.clear(); },
        [](BoulderShapeOptions& o) { o.styles[0].weight = -1.0; },
        [](BoulderShapeOptions& o) {
            for (auto& s : o.styles) {
                s.weight = 0.0;
            }
        },
        [](BoulderShapeOptions& o) { o.styles[0].intermediate = {0.9, 0.5}; },
        [](BoulderShapeOptions& o) { o.styles[0].shortAxis = {0.0, 0.5}; },
        [](BoulderShapeOptions& o) { o.styles[0].rounding = {0.0, 2.0}; },
        [](BoulderShapeOptions& o) { o.styles[0].minFractures = 13; },
        [](BoulderShapeOptions& o) { o.styles[0].edgeSoftness = {0.0, 0.6}; },
        [](BoulderShapeOptions& o) { o.styles[0].faceRoughness = 0.6; },
        [](BoulderShapeOptions& o) { o.weathering = -1.0; },
        [](BoulderShapeOptions& o) { o.styles[0].maxFractureTilt = 2.0; },
        [](BoulderShapeOptions& o) { o.styles[0].noiseWavelength = 0.0; },
        [](BoulderShapeOptions& o) { o.styles[0].noiseOctaves = 0; },
        [](BoulderShapeOptions& o) { o.subdivisions = 7; }};
    for (const auto& change : changes) {
        BoulderShapeOptions options;
        change(options);
        EXPECT_THROW(terraingen::ValidateBoulderShapeOptions(options), std::invalid_argument);
    }
}

TEST(BoulderShapeSeed, GivesUniqueOrSharedShapes) {
    EXPECT_EQ(terraingen::BoulderShapeSeed(123456789012345ull), 123456789012345ull);
    std::set<std::uint64_t> shared;
    for (std::uint64_t id = 0; id < 1000; ++id) {
        const auto seed = terraingen::BoulderShapeSeed(id * 7919u, 16);
        EXPECT_LT(seed, 16u);
        shared.insert(seed);
    }
    EXPECT_EQ(shared.size(), 16u);
}

TEST(NormalizedBoulderShape, TurnsAndScalesARockToTheUnitForm) {
    terraingen::SceneMesh rock{
        terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube), {}, {"granite", {0.6, 0.6, 0.55}}};
    rock.mesh = terraingen::Scaled(rock.mesh, {2.0, 6.0, 3.0});
    for (auto& p : rock.mesh.positions) {
        p = p + terraingen::Vec3d{10.0, -4.0, 7.0};
    }
    const auto shape = terraingen::NormalizedBoulderShape(rock);
    const auto box = terraingen::Bounds(shape.mesh);
    EXPECT_NEAR(box.Extents().x, 1.0, 1e-12) << "the longer horizontal side along x";
    EXPECT_NEAR(shape.intermediate, 2.0 / 6.0, 1e-12);
    EXPECT_NEAR(shape.shortAxis, 3.0 / 6.0, 1e-12) << "z stays vertical";
    EXPECT_NEAR(box.min.x + box.max.x, 0.0, 1e-12) << "centred on its box";
    EXPECT_NEAR(box.min.z + box.max.z, 0.0, 1e-12);
    EXPECT_GT(terraingen::Volume(shape.mesh), 0.0) << "turning keeps the faces outward";
    EXPECT_EQ(shape.material.name, "granite");
    terraingen::SceneMesh flat = rock;
    for (auto& p : flat.mesh.positions) {
        p.z = 0.0;
    }
    EXPECT_THROW(terraingen::NormalizedBoulderShape(flat), std::invalid_argument);
}

TEST(GenerateBoulderShape, DrawsFromTheLibraryWhenItHasRocks) {
    BoulderShapeOptions options;
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto pyramid = terraingen::BaseSolidMesh(terraingen::BaseSolid::Pyramid);
    options.library = {std::make_shared<const terraingen::SceneMesh>(
                           terraingen::SceneMesh{cube, {}, {"cube", {0.5, 0.5, 0.5}}}),
                       std::make_shared<const terraingen::SceneMesh>(
                           terraingen::SceneMesh{pyramid, {}, {"pyramid", {0.5, 0.5, 0.5}}})};
    std::set<std::size_t> picked;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const auto shape = GenerateBoulderShape(seed, options);
        picked.insert(shape.style);
        EXPECT_EQ(shape.material.name, shape.style == 0 ? "cube" : "pyramid");
        EXPECT_EQ(shape.mesh.positions.size(), options.library[shape.style]->mesh.positions.size());
        EXPECT_EQ(GenerateBoulderShape(seed, options).mesh.positions, shape.mesh.positions);
    }
    EXPECT_EQ(picked.size(), 2u) << "every rock is used";
    options.library.push_back(nullptr);
    EXPECT_THROW(terraingen::ValidateBoulderShapeOptions(options), std::invalid_argument);
}

TEST(GenerateBoulderShape, WeatheringWearsEdgesAwayAndZeroKeepsThemSharp) {
    BoulderShapeOptions sharp;
    sharp.weathering = 0.0;
    BoulderShapeOptions worn;
    worn.weathering = 2.0;
    for (const std::uint64_t seed : {0u, 1u, 9u}) {
        const auto a = terraingen::GenerateBoulderShape(seed, sharp);
        const auto b = terraingen::GenerateBoulderShape(seed, worn);
        ASSERT_EQ(a.style, b.style);
        EXPECT_LT(SharpestEdge(b.mesh), 0.8 * SharpestEdge(a.mesh)) << "worn edges are rounder";
    }
    BoulderShapeOptions plain;
    plain.styles = {terraingen::BoulderStyle{.name = "flat",
                                             .solid = terraingen::BaseSolid::Cube,
                                             .minFractures = 2,
                                             .maxFractures = 2}};
    plain.styles[0].noiseAmplitude = 0.0;
    const auto box = terraingen::GenerateBoulderShape(3, plain);
    std::size_t onTop = 0;
    for (const auto& p : box.mesh.positions) {
        onTop += std::fabs(p.z - terraingen::Bounds(box.mesh).max.z) < 1e-9 ? 1u : 0u;
    }
    EXPECT_GT(onTop, 3u) << "without softness or roughness, faces stay flat";
}
