// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Fade Crater Rays and Ejecta With Age
///
/// `CraterAlbedo` brightens the interiors, ejecta and rays of a crater layer's fresh craters by
/// their optical maturity, with Tycho's values by default. The bright material fades over a
/// retention time set by the crater's diameter, 5 Myr for craters up to 1 km, so a crater 3 Myr old
/// keeps only faint rays. Crater albedo needs no heights.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, FadeCraterRaysWithAge) {
    // --8<-- [start:fade_crater_rays_with_age]
    const auto craters = CraterLayer::Manual({
        ManualCrater(-900.0, 0.0, 400.0),
        ManualCrater(900.0, 0.0, 400.0).AgeGyr(0.003),  // 3 Myr of a 5 Myr retention
    });
    CraterAlbedoOptions options;
    options.seed = 2;
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<CraterAlbedo>(craters, options)};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {3600.0, 1800.0}, 10.0);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:fade_crater_rays_with_age]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_GT(bands[0](90, 90), bands[0](270, 90)) << "the fresh interior brighter than the old";
    EXPECT_GT(bands[0](270, 90), 0.11f);
}

}  // namespace
}  // namespace terraingen
