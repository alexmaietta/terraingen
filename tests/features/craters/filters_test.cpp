#include "terraingen/craters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::Crater;
using terraingen::CraterList;
using terraingen::CratersConflict;
using terraingen::CraterSpacing;
using terraingen::CraterTakesPrecedence;
using terraingen::RandomStream;
using terraingen::SpacingConflicts;
using terraingen::SpacingReach;
using terraingen::ThinBySpacing;

namespace {

CraterList List(const std::vector<Crater>& craters) {
    CraterList list;
    for (const Crater& crater : craters) {
        list.Append(crater);
    }
    return list;
}

}  // namespace

TEST(CratersConflict, NeedsSimilarSizesWithinTheSeparation) {
    const CraterSpacing spacing{1.5, 0.5};
    const Crater a{0.0, 0.0, 10.0, 0.0, 1u};
    EXPECT_TRUE(CratersConflict(a, {4.9, 0.0, 10.0, 0.0, 2u}, spacing));
    EXPECT_FALSE(CratersConflict(a, {5.0, 0.0, 10.0, 0.0, 2u}, spacing));
    EXPECT_TRUE(CratersConflict(a, {0.0, 7.4, 15.0, 0.0, 2u}, spacing));
    EXPECT_FALSE(CratersConflict(a, {0.0, 1.0, 15.1, 0.0, 2u}, spacing));
    EXPECT_FALSE(CratersConflict(a, {0.0, 1.0, 6.0, 0.0, 2u}, spacing));
    const Crater b{3.0, 0.0, 12.0, 0.0, 2u};
    EXPECT_EQ(CratersConflict(a, b, spacing), CratersConflict(b, a, spacing));
}

TEST(CraterTakesPrecedence, PrefersYoungerThenLargerThenSmallerId) {
    EXPECT_TRUE(CraterTakesPrecedence({0, 0, 5, 0.1, 9u}, {0, 0, 50, 0.2, 1u}));
    EXPECT_TRUE(CraterTakesPrecedence({0, 0, 50, 0.2, 9u}, {0, 0, 5, 0.2, 1u}));
    EXPECT_TRUE(CraterTakesPrecedence({0, 0, 5, 0.2, 1u}, {0, 0, 5, 0.2, 9u}));
    EXPECT_FALSE(CraterTakesPrecedence({0, 0, 5, 0.2, 9u}, {0, 0, 5, 0.2, 1u}));
}

TEST(SpacingConflicts, RemovesEveryCraterDominatedByAConflictingCrater) {
    const CraterSpacing spacing{1.5, 0.5};
    const CraterList chain = List({{0.0, 0.0, 10.0, 0.0, 1u},
                                   {4.0, 0.0, 10.0, 1.0, 2u},
                                   {8.0, 0.0, 10.0, 2.0, 3u},
                                   {100.0, 0.0, 10.0, 3.0, 4u}});
    EXPECT_EQ(SpacingConflicts(chain, spacing), (std::vector<bool>{false, true, true, false}));
    const CraterList reversed = List({chain.At(3), chain.At(2), chain.At(1), chain.At(0)});
    EXPECT_EQ(SpacingConflicts(reversed, spacing), (std::vector<bool>{false, true, true, false}));
    const CraterList thinned = ThinBySpacing(chain, spacing);
    ASSERT_EQ(thinned.Size(), 2u);
    EXPECT_EQ(thinned.At(0).id, 1u);
    EXPECT_EQ(thinned.At(1).id, 4u);
}

TEST(SpacingConflicts, MatchesABruteForceSearch) {
    const CraterSpacing spacing{1.8, 0.75};
    RandomStream random(12u, 3u);
    CraterList craters;
    for (std::uint64_t id = 0; id < 600; ++id) {
        craters.Append({random.NextDouble() * 300.0 - 150.0, random.NextDouble() * 300.0 - 150.0,
                        std::exp(random.NextDouble() * std::log(60.0)) + 0.5,
                        std::floor(random.NextDouble() * 4.0), id});
    }
    const std::vector<bool> conflicts = SpacingConflicts(craters, spacing);
    std::size_t removed = 0;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        bool expected = false;
        for (std::size_t m = 0; m < craters.Size() && !expected; ++m) {
            expected = m != n && CratersConflict(craters.At(n), craters.At(m), spacing) &&
                       CraterTakesPrecedence(craters.At(m), craters.At(n));
        }
        EXPECT_EQ(conflicts[n], expected) << n;
        removed += expected ? 1u : 0u;
    }
    EXPECT_GT(removed, 10u);
}

TEST(SpacingConflicts, IsANoOpWhenDisabledAndRejectsInvalidSpacing) {
    const CraterList pair = List({{0.0, 0.0, 10.0, 0.0, 1u}, {0.1, 0.0, 10.0, 0.0, 2u}});
    EXPECT_EQ(SpacingConflicts(pair, CraterSpacing{}), (std::vector<bool>{false, false}));
    EXPECT_EQ(ThinBySpacing(pair, CraterSpacing{}).Size(), 2u);
    EXPECT_THROW(SpacingConflicts(pair, CraterSpacing{0.9, 0.5}), std::invalid_argument);
    EXPECT_THROW(SpacingConflicts(pair, CraterSpacing{1.5, -0.1}), std::invalid_argument);
    EXPECT_THROW(
        SpacingConflicts(pair, CraterSpacing{std::numeric_limits<double>::infinity(), 0.5}),
        std::invalid_argument);
}

TEST(SpacingReach, BoundsTheDistanceOfAnyConflict) {
    const CraterSpacing spacing{2.0, 0.5};
    EXPECT_DOUBLE_EQ(SpacingReach(10.0, 100.0, spacing), 10.0);
    EXPECT_DOUBLE_EQ(SpacingReach(80.0, 100.0, spacing), 50.0);
}

TEST(CraterTakesPrecedence, PrefersManualCratersOverEverythingElse) {
    const Crater manual{0, 0, 5, 3.0, 9u, true};
    const Crater generated{0, 0, 50, 0.0, 1u};
    EXPECT_TRUE(CraterTakesPrecedence(manual, generated));
    EXPECT_FALSE(CraterTakesPrecedence(generated, manual));
}

TEST(SpacingConflicts, NeverRemovesManualCraters) {
    const CraterSpacing spacing{1.5, 1.0};
    const CraterList craters = List({{0.0, 0.0, 10.0, 2.0, 1u, true},
                                     {3.0, 0.0, 10.0, 1.0, 2u, true},
                                     {6.0, 0.0, 10.0, 0.0, 3u}});
    EXPECT_EQ(SpacingConflicts(craters, spacing), (std::vector<bool>{false, false, true}));
}
