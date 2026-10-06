#include "terraingen/meshes/boulder.h"

#include "tests/unit/meshes/support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <vector>

using terraingen::BoulderShapeOptions;
using terraingen::BoulderStyle;
using terraingen::GenerateBoulderShape;

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
        [](BoulderShapeOptions& o) { o.styles[0].minFractures = 9; },
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
