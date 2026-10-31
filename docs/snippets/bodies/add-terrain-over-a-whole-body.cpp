// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Add Relief, Craters and Boulders Over a Whole Body
///
/// Layers on a body cover all of it. The terrain takes the body as its surface, here with the
/// shape upsampled as its source, and the crater and boulder layers take it as their base.
/// Noise is 3D at each point, so it has no seams, and craters and boulders are drawn per cube-face
/// cell by that cell's true area. Grids and regions are longitude and latitude in degrees, and
/// spacings are meters. Here fresh craters from 3 m to 40 m and boulders from 1 m to 15 m cover a
/// lumpy body about 600 m long.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, AddTerrainOverAWholeBody) {
    const Surface body = Surface::ShapeModel(std::make_shared<RadialShape>(RadialShape::Sample(
        32, [](const Vec3d& u) { return 220.0 + 80.0 * u.x * u.x + 15.0 * u.y * u.z; })));
    // --8<-- [start:add_terrain_over_a_whole_body]
    Fractal fractal = Fractal::Fbm(5, 40.0, 0.6);  // seed, wavelength and amplitude in meters
    fractal.octaves = 4;
    const auto relief = std::make_shared<NoiseLayer>(fractal);
    CraterModels models = DefaultCraterModels();
    models.density = std::make_shared<PowerLawSfd>(0.05, 2.0);
    models.ages = std::make_shared<FreshAges>();
    CraterLayerOptions craterOptions;
    craterOptions.seed = 6;
    craterOptions.generation.minDiameter = 3.0;  // meters
    craterOptions.generation.maxDiameter = 40.0;
    const auto craters = std::make_shared<CraterLayer>(models, craterOptions, body);
    BoulderLayerOptions boulderOptions;
    boulderOptions.seed = 7;
    boulderOptions.sizes.distribution = std::make_shared<PowerLawSfd>(0.05, 2.5);
    boulderOptions.sizes.minDiameter = 1.0;
    boulderOptions.sizes.maxDiameter = 15.0;
    const auto boulders = std::make_shared<BoulderLayer>(boulderOptions, craters, body);
    const auto shape = std::make_shared<ShapeModelSource>(body);  // the shape, upsampled by cubic
    const Terrain terrain(body, shape, {relief, craters, boulders});

    const Bounds2d whole{-180.0, -90.0, 180.0, 90.0};  // longitude and latitude, degrees
    const Raster<float> heights =
        terrain.Evaluate(RasterGrid::Covering(whole, 10.0), EvaluationOptions{}.Spacing(0.5));
    const CraterList listed = craters->Craters(whole, 0.5);  // every crater on the body
    // --8<-- [end:add_terrain_over_a_whole_body]
    EXPECT_EQ(heights.Width(), 36u);
    EXPECT_EQ(heights.Height(), 18u);
    EXPECT_GT(listed.Size(), 100u);
}

}  // namespace
}  // namespace terraingen
