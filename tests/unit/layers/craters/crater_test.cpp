// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using terraingen::Crater;
using terraingen::CraterSelection;

namespace {

/// A selection of every diameter, aged from `minAgeGyr` to `maxAgeGyr`.
CraterSelection Ages(double minAgeGyr, double maxAgeGyr = std::numeric_limits<double>::infinity()) {
    CraterSelection selection;
    selection.minAgeGyr = minAgeGyr;
    selection.maxAgeGyr = maxAgeGyr;
    return selection;
}

}  // namespace

TEST(CraterSelection, IncludesCratersWithinItsRanges) {
    const Crater crater{0.0, 0.0, 100.0, 1.0, 7u};
    EXPECT_TRUE(CraterSelection{}.Includes(crater));
    EXPECT_FALSE(CraterSelection{200.0}.Includes(crater));
    EXPECT_FALSE((CraterSelection{0.0, 50.0}).Includes(crater));
    const CraterSelection exact{100.0, 100.0};
    EXPECT_TRUE(exact.Includes(crater));
    EXPECT_FALSE(Ages(2.0).Includes(crater));
    EXPECT_FALSE(Ages(0.0, 0.5).Includes(crater));
    Crater manual = crater;
    manual.manual = true;
    EXPECT_TRUE(CraterSelection{}.Includes(manual));
    CraterSelection generated;
    generated.manual = false;
    EXPECT_FALSE(generated.Includes(manual));
    EXPECT_TRUE(generated.Includes(crater));
}

TEST(ValidateCraterSelection, RejectsInvertedOrNegativeRangesNamingTheModel) {
    EXPECT_NO_THROW(terraingen::ValidateCraterSelection({}, "Test"));
    for (const CraterSelection& invalid :
         {CraterSelection{-1.0}, CraterSelection{2.0, 1.0}, Ages(-0.1), Ages(2.0, 1.0)}) {
        try {
            terraingen::ValidateCraterSelection(invalid, "Test");
            ADD_FAILURE() << "accepted an invalid selection";
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string(error.what()),
                      "Test size and age ranges must satisfy 0 <= min <= max");
        }
    }
}

TEST(Crater, LeavesDepthAndRimHeightUnsetAndComparesNanAsEqual) {
    const Crater plain{1.0, 2.0, 30.0, 0.5, 4u};
    EXPECT_TRUE(std::isnan(plain.depth));
    EXPECT_TRUE(std::isnan(plain.rimHeight));
    EXPECT_EQ(plain, plain);
    Crater deep = plain;
    deep.depth = 3.0;
    EXPECT_NE(deep, plain);
    Crater rimmed = deep;
    rimmed.rimHeight = 0.5;
    EXPECT_NE(rimmed, deep);
    EXPECT_EQ(rimmed, rimmed);
}

TEST(CraterList, KeepsDepthAndRimHeightColumns) {
    terraingen::CraterList list;
    list.Reserve(2);
    list.Append(Crater{0.0, 0.0, 10.0, 0.0, 1u, true, 1.5, 0.25});
    list.Append(Crater{5.0, 0.0, 20.0, 1.0, 2u});
    ASSERT_EQ(list.Depths().size(), 2u);
    EXPECT_EQ(list.Depths()[0], 1.5);
    EXPECT_EQ(list.RimHeights()[0], 0.25);
    EXPECT_TRUE(std::isnan(list.Depths()[1]));
    EXPECT_TRUE(std::isnan(list.RimHeights()[1]));
    terraingen::CraterList copy;
    copy.Append(list);
    EXPECT_EQ(copy.At(0), list.At(0));
    EXPECT_EQ(copy.At(1), list.At(1));
    EXPECT_EQ(copy.At(0).depth, 1.5);
    EXPECT_EQ(copy.At(0).rimHeight, 0.25);
}

TEST(AppendManualCraters, KeepsExplicitDepthsAndRimHeights) {
    terraingen::CraterList manual;
    manual.Append(Crater{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9});
    manual.Append(Crater{9.0, 0.0, 6.0, 0.0, 0u, false, 0.9, 0.0});
    terraingen::CraterList craters;
    terraingen::AppendManualCraters(manual, craters);
    ASSERT_EQ(craters.Size(), 2u);
    EXPECT_EQ(craters.At(0).depth, 0.9);
    EXPECT_TRUE(std::isnan(craters.At(0).rimHeight));
    EXPECT_EQ(craters.At(1).rimHeight, 0.0);
    EXPECT_EQ(craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 6.0))
        << "the ID ignores the depth";
}

TEST(AppendManualCraters, RejectsInvalidDepthsAndRimHeights) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const struct {
        double depth;
        double rimHeight;
        const char* message;
    } cases[] = {
        {0.0, nan, "A manual crater's depth must be unset or positive"},
        {-1.0, nan, "A manual crater's depth must be unset or positive"},
        {inf, nan, "A manual crater's depth must be unset or positive"},
        {1.0, -0.1, "A manual crater's rim height must be unset or not negative"},
        {1.0, inf, "A manual crater's rim height must be unset or not negative"},
        {nan, 0.2, "A manual crater's rim height needs its depth too"},
    };
    for (const auto& c : cases) {
        terraingen::CraterList manual;
        manual.Append(Crater{0.0, 0.0, 6.0, 0.0, 0u, false, c.depth, c.rimHeight});
        terraingen::CraterList craters;
        try {
            terraingen::AppendManualCraters(manual, craters, [](const Crater&) { return false; });
            ADD_FAILURE() << "accepted depth " << c.depth << ", rim " << c.rimHeight;
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string(error.what()), c.message);
        }
    }
}

TEST(ManualCrater, FlagsAHandPlacedCraterWithItsDepthAndRim) {
    const Crater plain = terraingen::ManualCrater(1.0, -2.0, 30.0);
    EXPECT_EQ(plain, (Crater{1.0, -2.0, 30.0, 0.0, 0u, true}));
    EXPECT_TRUE(std::isnan(plain.depth));
    EXPECT_TRUE(std::isnan(plain.rimHeight));
    const Crater deep = terraingen::ManualCrater(1.0, -2.0, 30.0, 6.0, 1.2);
    EXPECT_TRUE(deep.manual);
    EXPECT_EQ(deep.depth, 6.0);
    EXPECT_EQ(deep.rimHeight, 1.2);
}

TEST(CraterList, BuildsFromAListOfCratersInOrder) {
    const terraingen::CraterList empty{};
    EXPECT_TRUE(empty.Empty());
    const terraingen::CraterList list{terraingen::ManualCrater(0.0, 0.0, 10.0, 2.0),
                                      Crater{5.0, 6.0, 7.0, 1.5, 42u}};
    ASSERT_EQ(list.Size(), 2u);
    EXPECT_EQ(list.At(0), terraingen::ManualCrater(0.0, 0.0, 10.0, 2.0));
    EXPECT_EQ(list.At(1), (Crater{5.0, 6.0, 7.0, 1.5, 42u}));
}
