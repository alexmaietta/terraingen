// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Add Reflectance Zones Around a New Crater
///
/// New craters up to about 50 m across leave reflectance zones that LROC before and after images
/// show, a bright proximal zone, a dark one beyond it and faint finger-like distal zones reaching
/// a hundred diameters out. They are off by default and switched on in the crater albedo's
/// options. The zones fade with the crater, and like it they are left out where the sample
/// spacing cannot resolve the crater.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, AddReflectanceZonesAroundANewCrater) {
    const auto crater = CraterLayer::Manual({ManualCrater(0.0, 0.0, 20.0)});
    // --8<-- [start:add_reflectance_zones_around_a_new_crater]
    CraterAlbedoOptions options;
    options.seed = 2;
    options.zones.enabled = true;
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<CraterAlbedo>(crater, options)};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {4000.0, 4000.0}, 5.0);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:add_reflectance_zones_around_a_new_crater]
    ASSERT_EQ(bands.size(), 1u);
    const Span<const float> values = bands[0].Values();
    const auto range = std::minmax_element(values.begin(), values.end());
    EXPECT_LT(*range.first, 0.11f) << "the dark zone";
    EXPECT_GT(*range.second, 0.11f) << "the bright zone";
}

}  // namespace
}  // namespace terraingen
