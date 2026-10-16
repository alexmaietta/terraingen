#include "terraingen/distribution/combined.h"

#include "terraingen/distribution/power_law.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::DiameterRange;
using terraingen::PowerLawSegment;
using terraingen::PowerLawSfd;
using terraingen::SfdKind;
using terraingen::SizeFrequencyDistribution;

TEST(PowerLawSegment, EvaluatesAndInvertsItsLaw) {
    const PowerLawSegment segment{1.0, 8.0, 3.0};
    EXPECT_DOUBLE_EQ(segment.Cumulative(2.0), 1.0);
    EXPECT_DOUBLE_EQ(segment.Incremental(2.0), 8.0 * 3.0 / 16.0);
    EXPECT_DOUBLE_EQ(segment.Invert(1.0), 2.0);
}

TEST(CommonKind, NeedsOneKindAndNoNulls) {
    const PowerLawSfd density(1.0, 2.0);
    const PowerLawSfd rate(1.0, 2.0, SfdKind::ProductionRate);
    const std::vector<const SizeFrequencyDistribution*> same = {&density, &density};
    EXPECT_EQ(terraingen::detail::CommonKind(same, "A test"), SfdKind::Density);
    const std::vector<const SizeFrequencyDistribution*> none;
    EXPECT_THROW(terraingen::detail::CommonKind(none, "A test"), std::invalid_argument);
    const std::vector<const SizeFrequencyDistribution*> null = {&density, nullptr};
    EXPECT_THROW(terraingen::detail::CommonKind(null, "A test"), std::invalid_argument);
    const std::vector<const SizeFrequencyDistribution*> mixed = {&density, &rate};
    EXPECT_THROW(terraingen::detail::CommonKind(mixed, "A test"), std::invalid_argument);
}

TEST(UsableRange, IsEverywhereForAnExtrapolatingDistribution) {
    PowerLawSfd law(1.0, 2.0, SfdKind::Density, DiameterRange{2.0, 5.0});
    EXPECT_EQ(terraingen::detail::UsableRange(law).min, 2.0);
    law.SetAllowExtrapolation(true);
    EXPECT_EQ(terraingen::detail::UsableRange(law).min, 0.0);
    EXPECT_TRUE(std::isinf(terraingen::detail::UsableRange(law).max));
}

TEST(CheckJoin, RejectsRisesAndUnallowedDrops) {
    EXPECT_NO_THROW(terraingen::detail::CheckJoin(1.0, 1.0005, 10.0, 1e-3, false));
    EXPECT_THROW(terraingen::detail::CheckJoin(1.0, 1.01, 10.0, 1e-3, false),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::detail::CheckJoin(1.0, 0.5, 10.0, 1e-3, false), std::invalid_argument);
    EXPECT_NO_THROW(terraingen::detail::CheckJoin(1.0, 0.5, 10.0, 1e-3, true));
}

TEST(IndexOfStart, AppliesTheLowerPieceAtAJoin) {
    const std::vector<PowerLawSegment> pieces = {
        {0.0, 1.0, 2.0}, {10.0, 1.0, 2.0}, {100.0, 1.0, 2.0}};
    EXPECT_EQ(terraingen::detail::IndexOfStart(pieces, 5.0), 0u);
    EXPECT_EQ(terraingen::detail::IndexOfStart(pieces, 10.0), 0u);
    EXPECT_EQ(terraingen::detail::IndexOfStart(pieces, 10.5), 1u);
    EXPECT_EQ(terraingen::detail::IndexOfStart(pieces, 1e6), 2u);
}
