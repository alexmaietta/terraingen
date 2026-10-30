// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Evaluate the Albedo of a Configuration Without Heights
///
/// A configuration's albedo is a source of starting values changed by albedo layers, here fresh
/// craters brightened above a synthetic base. `BuildAlbedo` builds it for the terrain built from
/// the same configuration, whose crater layers it reads. Layers that depend only on craters need
/// no heights, so the albedo is evaluated over any grid alone.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, EvaluateAConfiguredAlbedo) {
    // --8<-- [start:evaluate_a_configured_albedo]
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}],
        "albedo": {"source": {"type": "synthetic", "value": 0.12}, "layers": [{"type": "craters"}]}
    })");
    const Terrain terrain = BuildTerrain(config);
    const Albedo albedo = BuildAlbedo(config, terrain);
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {100.0, 100.0}, 0.5);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:evaluate_a_configured_albedo]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_EQ(bands[0].Width(), 200);
}

}  // namespace
}  // namespace terraingen
