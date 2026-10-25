// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief List the Boulders of a Configuration Without Evaluating Heights
///
/// A terrain's boulder layers list the boulders they place over any grid, each with its size,
/// burial, orientation and the ground height beneath it. The ground is read at each boulder alone,
/// from the layers beneath it, so no heightmap is evaluated. Ejecta boulders come from the craters
/// of the layers before.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, ListBouldersFromAConfig) {
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "layers": [
            {"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40},
            {"type": "boulders", "min_diameter_m": 0.3, "max_diameter_m": 3}
        ]
    })");
    const std::filesystem::path path = OutputFolder() / "boulders.json";

    // --8<-- [start:list_boulders_from_a_config]
    const TerrainGenerator terrain = BuildGenerator(config);
    const RasterGrid site = RasterGrid::Centered({0.0, 0.0}, {100.0, 100.0}, 0.5);
    const BoulderFeatures placed = PlacedBoulders(terrain, site);
    const std::vector<double>& grounds = placed.boulders.Grounds();  // meters
    WriteBoulderList(placed.boulders, path, ListFormat::Json);
    // --8<-- [end:list_boulders_from_a_config]
    EXPECT_GT(grounds.size(), 0u);
    EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace terraingen
