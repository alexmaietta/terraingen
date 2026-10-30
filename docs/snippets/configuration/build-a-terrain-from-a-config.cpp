// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Build a Terrain From a Configuration
///
/// A configuration is JSON that names the body a terrain lies on, a source of starting heights and
/// layers. `BuildTerrain` turns it into the library objects it names, checking it first, so they
/// can be listed, queried or changed without generating any outputs. `LoadConfig` reads the same
/// JSON from a file.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, BuildATerrainFromAConfig) {
    // --8<-- [start:build_a_terrain_from_a_config]
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "body": {"type": "sphere", "radius_m": 1737400},
        "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}]
    })");
    const Terrain terrain = BuildTerrain(config);
    // --8<-- [end:build_a_terrain_from_a_config]
    EXPECT_TRUE(terrain.Surface().IsBody());
    EXPECT_EQ(terrain.Layers().size(), 1u);
}

}  // namespace
}  // namespace terraingen
