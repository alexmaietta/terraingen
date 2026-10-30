// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Weather Boulders With Fillets and Ventifacts
///
/// Fillets are aprons of regolith banked against each boulder's base, rising to a share of its
/// exposed height. Ventifacts are facets that sand-laden wind planes on a boulder's windward side,
/// facing upwind. Both are drawn into the heights, the facets cutting the tops of the boulders'
/// shapes.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WeatherBouldersWithFilletsAndVentifacts) {
    std::vector<ManualBoulder> catalog;
    for (const double x : {-3.0, 0.0, 3.0}) {
        catalog.push_back(ManualBoulder(x, 0.0, 1.5).Burial(0.2));
    }
    BoulderLayerOptions options;

    // --8<-- [start:weather_boulders_with_fillets_and_ventifacts]
    options.fillet.heightShare = 0.4;  // regolith aprons
    options.fillet.widthRatio = 0.3;
    options.ventifacts.share = 1.0;
    options.ventifacts.windAzimuthDegrees = 0.0;      // blowing toward the east
    options.rendering.relief = BoulderRelief::Shape;  // facets cut the shapes' tops
    const auto boulders = BoulderLayer::Manual(catalog, options);
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(501, 201, 0.02), {boulders});
    const Raster<float> heights = terrain.Evaluate();
    const BoulderList list = boulders->Boulders({-5.0, -2.0, 5.0, 2.0});
    // --8<-- [end:weather_boulders_with_fillets_and_ventifacts]
    EXPECT_GT(heights(250 + 42, 100), 0.0f);  // a fillet just beyond the middle boulder
    for (const std::uint8_t count : list.FacetCounts()) {
        EXPECT_EQ(count, 1u);
    }
}

}  // namespace
}  // namespace terraingen
