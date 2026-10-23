// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using terraingen::Boulder;
using terraingen::BoulderList;

namespace {

/// A boulder with every field set, ground and shape seed included.
Boulder Posed() {
    Boulder b;
    b.x = 1.0;
    b.y = -2.0;
    b.diameter = 1.5;
    b.height = 0.9;
    b.burial = 0.3;
    b.yaw = 0.4;
    b.lean = 0.1;
    b.id = 42;
    b.rest = terraingen::BoulderRest::Side;
    b.tilt = 0.2;
    b.tiltAzimuth = 1.1;
    b.ground = 3.5;
    b.model = true;
    b.wear = 0.6;
    b.parent = 7;
    b.piece = 2;
    b.facets[0] = {0.5, 0.6, 0.7};
    b.facetCount = 1;
    b.shapeSeed = 99u;
    return b;
}

}  // namespace

TEST(BoulderList, RoundTripsEveryField) {
    const Boulder plain{2.0, 3.0, 0.5, 0.4};
    const BoulderList list{Posed(), plain};
    ASSERT_EQ(list.Size(), 2u);
    EXPECT_EQ(list.At(0), Posed());
    EXPECT_EQ(list.At(1), plain);
    EXPECT_FALSE(list.At(1).ground.has_value());
    EXPECT_FALSE(list.At(1).shapeSeed.has_value());
    EXPECT_THROW(list.At(2), std::out_of_range);
}

TEST(BoulderList, StoresOneColumnPerField) {
    const BoulderList list{Posed(), Boulder{2.0, 3.0, 0.5, 0.4}};
    EXPECT_EQ(list.X(), (std::vector<double>{1.0, 2.0}));
    EXPECT_EQ(list.Diameters(), (std::vector<double>{1.5, 0.5}));
    EXPECT_EQ(list.Ids(), (std::vector<std::uint64_t>{42, 0}));
    EXPECT_EQ(list.Rests(), (std::vector<std::uint8_t>{1, 0}));
    EXPECT_EQ(list.Models(), (std::vector<std::uint8_t>{1, 0}));
    EXPECT_EQ(list.Grounds()[0], 3.5);
    EXPECT_TRUE(std::isnan(list.Grounds()[1]));
    EXPECT_EQ(list.HasShapeSeeds(), (std::vector<std::uint8_t>{1, 0}));
    EXPECT_EQ(list.FacetCounts(), (std::vector<std::uint8_t>{1, 0}));
}

TEST(BoulderList, AppendsListsInOrderAndCompares) {
    BoulderList list;
    EXPECT_TRUE(list.Empty());
    list.Reserve(3);
    list.Append(Posed());
    list.Append(BoulderList{Boulder{2.0, 3.0, 0.5, 0.4}, Posed()});
    ASSERT_EQ(list.Size(), 3u);
    EXPECT_EQ(list.At(2), Posed());
    EXPECT_EQ(list, (BoulderList{Posed(), Boulder{2.0, 3.0, 0.5, 0.4}, Posed()}));
    EXPECT_NE(list, (BoulderList{Posed()}));
}
