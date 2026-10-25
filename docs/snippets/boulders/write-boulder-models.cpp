// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write the Boulders of a Configuration as 3D Models
///
/// Boulders at or above a model threshold become 3D models of their own, each with its own shape
/// or one of a few shared prototypes, settled onto the ground beneath it. Written as glTF, each
/// shape is stored once and drawn per boulder. On a body the scene is in meters east, north and up
/// of a site, here the grid's center.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteBoulderModels) {
    const std::filesystem::path path = OutputFolder() / "boulders.glb";

    // --8<-- [start:write_boulder_models]
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "body": {"type": "sphere", "radius_m": 1737400},
        "layers": [
            {"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40},
            {"type": "boulders", "min_diameter_m": 0.3, "max_diameter_m": 3,
             "model_threshold": {"min_diameter_m": 1.5}, "shape_prototypes": 8}
        ]
    })");
    const TerrainGenerator terrain = BuildGenerator(config);
    const Vec2d center{30.0, -10.0};  // longitude and latitude in degrees
    const RasterGrid site = RasterGrid::Centered(center, {0.004, 0.004}, 0.0001);
    const std::vector<BoulderModel> models = PlacedBoulders(terrain, site).models;
    WriteBoulderModels(models, path, 60.0, terrain.surface, center);
    // --8<-- [end:write_boulder_models]
    EXPECT_GT(models.size(), 0u);
    EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace terraingen
