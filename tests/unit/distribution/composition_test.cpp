// SPDX-License-Identifier: BSD-3-Clause

#include "distribution/composition.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/summed.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::DiameterRange;
using terraingen::PowerLawSegment;
using terraingen::PowerLawSfd;
using terraingen::SfdKind;
using terraingen::SizeFrequencyDistribution;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

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

TEST(Distributions, ListsPointersAndPartsAlike) {
    const auto law = std::make_shared<const PowerLawSfd>(1.0, 2.0);
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>> pointers = {law, nullptr};
    EXPECT_EQ(terraingen::detail::Distributions(pointers),
              (std::vector<const SizeFrequencyDistribution*>{law.get(), nullptr}));
    const std::vector<terraingen::WeightedSfd> terms = {{2.0, law}};
    EXPECT_EQ(terraingen::detail::Distributions(terms),
              (std::vector<const SizeFrequencyDistribution*>{law.get()}));
}

TEST(IntersectRanges, KeepsTheOverlapOrThrows) {
    const std::vector<DiameterRange> ranges = {{1.0, 10.0}, {2.0, kInf}, {0.0, 8.0}};
    const DiameterRange overlap = terraingen::detail::IntersectRanges(ranges, "none");
    EXPECT_EQ(overlap.min, 2.0);
    EXPECT_EQ(overlap.max, 8.0);
    const std::vector<DiameterRange> apart = {{1.0, 2.0}, {2.0, 3.0}};
    EXPECT_THROW(terraingen::detail::IntersectRanges(apart, "none"), std::invalid_argument);
}

TEST(CheckStarts, NeedsTheRangeMinimumThenIncreasingStartsInsideTheRange) {
    const DiameterRange range{1.0, 100.0};
    const std::vector<double> good = {1.0, 10.0, 50.0};
    EXPECT_NO_THROW(terraingen::detail::CheckStarts(good, range, "Piece"));
    const std::vector<double> late = {2.0, 10.0};
    EXPECT_THROW(terraingen::detail::CheckStarts(late, range, "Piece"), std::invalid_argument);
    const std::vector<double> back = {1.0, 10.0, 10.0};
    try {
        terraingen::detail::CheckStarts(back, range, "Segment");
        FAIL() << "repeated start accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "Segment starts must increase within the range");
    }
    const std::vector<double> outside = {1.0, 100.0};
    EXPECT_THROW(terraingen::detail::CheckStarts(outside, range, "Piece"), std::invalid_argument);
    EXPECT_THROW(terraingen::detail::CheckStarts({}, range, "Piece"), std::invalid_argument);
}

TEST(CheckJoins, ChecksEveryJoinOfThePieces) {
    const auto cumulative = [](const PowerLawSegment& s, double d) { return s.Cumulative(d); };
    const std::vector<PowerLawSegment> continuous = {{1.0, 100.0, 2.0}, {10.0, 1000.0, 3.0}};
    EXPECT_NO_THROW(terraingen::detail::CheckJoins(continuous, cumulative, 1e-3, false));
    const std::vector<PowerLawSegment> rising = {{1.0, 100.0, 2.0}, {10.0, 2000.0, 3.0}};
    EXPECT_THROW(terraingen::detail::CheckJoins(rising, cumulative, 1e-3, true),
                 std::invalid_argument);
}

TEST(WalkPieces, StopsAtTheFirstPieceAcceptedOrTheLast) {
    const std::vector<PowerLawSegment> pieces = {
        {0.0, 1.0, 2.0}, {10.0, 1.0, 2.0}, {100.0, 1.0, 2.0}};
    std::vector<double> highs;
    const auto never = [&](std::size_t, double high) {
        highs.push_back(high);
        return false;
    };
    EXPECT_EQ(terraingen::detail::WalkPieces(pieces, 0, 50.0, never), 2u);
    EXPECT_EQ(highs, (std::vector<double>{10.0, 50.0}));
    const auto second = [](std::size_t index, double) { return index == 1; };
    EXPECT_EQ(terraingen::detail::WalkPieces(pieces, 0, kInf, second), 1u);
    const auto [low, high] = terraingen::detail::PieceInterval(pieces, 1, 5.0, 50.0);
    EXPECT_EQ(low, 10.0);
    EXPECT_EQ(high, 50.0);
    const auto [lastLow, lastHigh] = terraingen::detail::PieceInterval(pieces, 2, 150.0, kInf);
    EXPECT_EQ(lastLow, 150.0);
    EXPECT_TRUE(std::isinf(lastHigh));
}

TEST(PieceIndex, AppliesTheLowerPieceAtAJoin) {
    const std::vector<PowerLawSegment> pieces = {
        {0.0, 1.0, 2.0}, {10.0, 1.0, 2.0}, {100.0, 1.0, 2.0}};
    EXPECT_EQ(terraingen::detail::PieceIndex(pieces, 5.0), 0u);
    EXPECT_EQ(terraingen::detail::PieceIndex(pieces, 10.0), 0u);
    EXPECT_EQ(terraingen::detail::PieceIndex(pieces, 10.5), 1u);
    EXPECT_EQ(terraingen::detail::PieceIndex(pieces, 1e6), 2u);
}

TEST(IntersectRanges, IntersectsTheRangesOfParts) {
    const auto narrow =
        std::make_shared<const PowerLawSfd>(1.0, 2.0, SfdKind::Density, DiameterRange{2.0, 50.0});
    const auto wide =
        std::make_shared<const PowerLawSfd>(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, 20.0});
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>> parts = {narrow, wide};
    const DiameterRange valid = terraingen::detail::IntersectRanges(
        parts, [](const SizeFrequencyDistribution& each) { return each.ValidRange(); }, "none");
    EXPECT_EQ(valid.min, 2.0);
    EXPECT_EQ(valid.max, 20.0);
    const std::vector<terraingen::WeightedSfd> terms = {{1.0, narrow}};
    EXPECT_EQ(
        terraingen::detail::IntersectRanges(terms, terraingen::detail::UsableRange, "none").max,
        50.0);
}
