// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Brighten the Blast Zone Around a Lander
///
/// A lander's descent engine scours the regolith around it, leaving a zone brighter than the
/// undisturbed ground and a fainter halo beyond. `BlastZone` defaults to the Apollo mean, 28785
/// square meters about 11 percent brighter on average, and can add a dark core where the exhaust
/// dug in. Blast zones need no heights.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, BrightenALanderBlastZone) {
    // --8<-- [start:brighten_a_lander_blast_zone]
    BlastZone zone;
    zone.x = 0.0;
    zone.y = 0.0;
    zone.area = 28785.0;     // square meters
    zone.coreRadius = 15.0;  // meters
    zone.seed = 1;
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<BlastZoneAlbedo>(std::vector<BlastZone>{zone})};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {400.0, 400.0}, 1.0);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:brighten_a_lander_blast_zone]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_LT(bands[0](200, 200), 0.11f) << "the dark core under the lander";
    EXPECT_GT(bands[0](250, 200), 0.11f) << "the bright zone around it";
    EXPECT_FLOAT_EQ(bands[0](0, 0), 0.11f) << "undisturbed ground";
}

}  // namespace
}  // namespace terraingen
