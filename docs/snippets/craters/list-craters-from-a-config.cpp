// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief List the Craters of a Configuration Without Evaluating Heights
///
/// Craters are a pure function of position, so a terrain's crater layers list the craters they
/// would apply over any grid without computing a height. Each comes with its age,
/// floor depth, rim height and freshness. On a body the grid is in degrees of longitude and
/// latitude.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, ListCratersFromAConfig) {
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "body": {"type": "sphere", "radius_m": 1737400},
        "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}]
    })");
    const std::filesystem::path path = OutputFolder() / "craters.csv";

    // --8<-- [start:list_craters_from_a_config]
    const Terrain terrain = BuildTerrain(config);
    const RasterGrid site = RasterGrid::Centered({30.0, -10.0}, {0.004, 0.004}, 0.0001);
    const CraterFeatures placed = terrain.Craters(site);
    const std::vector<double>& diameters = placed.craters.Diameters();  // meters
    WriteCraterList(placed.craters, path, ListFormat::Csv, placed.attributes);
    // --8<-- [end:list_craters_from_a_config]
    EXPECT_GT(diameters.size(), 0u);
    EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace terraingen
