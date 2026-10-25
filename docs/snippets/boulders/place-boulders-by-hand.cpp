// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Place Boulders by Hand
///
/// Boulders from a catalog or placed by hand need only a center and a diameter. Any of their
/// height, burial, orientation, rest, wear or shape can be fixed, and the rest is drawn as for any
/// boulder. A manual layer places only these, posed and drawn as its options say.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cstddef>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, PlaceBouldersByHand) {
    // --8<-- [start:place_boulders_by_hand]
    const std::vector<ManualBoulder> catalog{
        ManualBoulder(0.0, 0.0, 2.0).Height(1.0).Burial(0.2).YawDegrees(30.0),
        ManualBoulder(2.2, 1.5, 1.2).Rest(BoulderRest::Side),
        ManualBoulder(-2.0, -1.5, 1.4).ShapeSeed(7),
        ManualBoulder(1.8, -2.0, 0.8),  // the rest of its pose drawn as for any boulder
    };
    BoulderLayerOptions options;
    options.rendering.relief = BoulderRelief::Shape;
    const auto boulders = BoulderLayer::Manual(catalog, options);  // no random boulders
    const BoulderList list = boulders->Boulders({-3.5, -3.5, 3.5, 3.5});
    // --8<-- [end:place_boulders_by_hand]
    ASSERT_EQ(list.Size(), 4u);
    bool found = false;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        const Boulder boulder = list.At(i);
        if (boulder.diameter == 2.0) {
            found = true;
            EXPECT_EQ(boulder.height, 1.0);
            EXPECT_EQ(boulder.burial, 0.2);
        }
    }
    EXPECT_TRUE(found);
}

}  // namespace
}  // namespace terraingen
