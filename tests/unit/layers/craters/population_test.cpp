#include "support/log_capture.h"
#include "support/lunar_craters.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::Bounds2d;
using terraingen::Crater;
using terraingen::CraterGenerationOptions;
using terraingen::CraterList;
using terraingen::FreshAges;
using terraingen::GenerateCraters;
using terraingen::PlanarCells;
using terraingen::PowerLawSfd;
using terraingen::SfdKind;
using terraingen::UniformAges;

namespace {

const PlanarCells kCells;

std::map<std::uint64_t, Crater> ById(const CraterList& craters) {
    std::map<std::uint64_t, Crater> byId;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        byId[craters.At(n).id] = craters.At(n);
    }
    return byId;
}

void ExpectSameCraters(const CraterList& a, const CraterList& b) {
    const auto left = ById(a);
    const auto right = ById(b);
    ASSERT_EQ(left.size(), a.Size());
    ASSERT_EQ(left.size(), right.size());
    for (const auto& [id, crater] : left) {
        const auto match = right.find(id);
        ASSERT_NE(match, right.end()) << id;
        EXPECT_EQ(crater.x, match->second.x);
        EXPECT_EQ(crater.y, match->second.y);
        EXPECT_EQ(crater.diameter, match->second.diameter);
        EXPECT_EQ(crater.ageGyr, match->second.ageGyr);
    }
}

CraterList Filter(const CraterList& craters, const Bounds2d& region, double minDiameter = 0.0) {
    CraterList kept;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        if (region.Contains(crater.x, crater.y) && crater.diameter >= minDiameter) {
            kept.Append(crater);
        }
    }
    return kept;
}

}  // namespace

TEST(CraterList, StoresOneColumnPerField) {
    CraterList list;
    EXPECT_TRUE(list.Empty());
    list.Append({1.0, 2.0, 3.0, 0.5, 7u});
    CraterList other;
    other.Append({4.0, 5.0, 6.0, 1.5, 9u});
    list.Append(other);
    ASSERT_EQ(list.Size(), 2u);
    EXPECT_EQ(list.X()[1], 4.0);
    EXPECT_EQ(list.Y()[0], 2.0);
    EXPECT_EQ(list.Diameters()[1], 6.0);
    EXPECT_EQ(list.AgesGyr()[0], 0.5);
    EXPECT_EQ(list.Ids()[1], 9u);
    EXPECT_EQ(list.At(0).id, 7u);
    EXPECT_THROW(list.At(2), std::out_of_range);
}

TEST(GenerateCraters, IsDeterministicPerSeedWithUniqueIds) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 500.0, 500.0};
    const CraterGenerationOptions options{.dMin = 2.0, .dMax = 64.0};
    const CraterList first = GenerateCraters(kCells, region, density, FreshAges(), 7u, options);
    const CraterList second = GenerateCraters(kCells, region, density, FreshAges(), 7u, options);
    const CraterList other = GenerateCraters(kCells, region, density, FreshAges(), 8u, options);
    ASSERT_GT(first.Size(), 100u);
    EXPECT_EQ(first.Ids(), second.Ids());
    EXPECT_EQ(first.X(), second.X());
    EXPECT_EQ(first.Diameters(), second.Diameters());
    EXPECT_NE(first.X(), other.X());
    EXPECT_EQ(std::set<std::uint64_t>(first.Ids().begin(), first.Ids().end()).size(), first.Size());
    for (std::size_t n = 0; n < first.Size(); ++n) {
        const Crater crater = first.At(n);
        EXPECT_TRUE(region.Contains(crater.x, crater.y));
        EXPECT_GE(crater.diameter, 2.0);
        EXPECT_LE(crater.diameter, 64.0);
        EXPECT_EQ(crater.ageGyr, 0.0);
    }
}

TEST(GenerateCraters, TilesJoinToTheWholeRegion) {
    const PowerLawSfd density(0.1, 2.0);
    const CraterGenerationOptions options{.dMin = 1.0, .dMax = 50.0};
    const CraterList whole =
        GenerateCraters(kCells, {-300.0, -200.0, 300.0, 200.0}, density, FreshAges(), 3u, options);
    CraterList joined;
    for (const Bounds2d& tile :
         {Bounds2d{-300.0, -200.0, 17.5, 0.0}, Bounds2d{17.5, -200.0, 300.0, 0.0},
          Bounds2d{-300.0, 0.0, 17.5, 200.0}, Bounds2d{17.5, 0.0, 300.0, 200.0}}) {
        joined.Append(GenerateCraters(kCells, tile, density, FreshAges(), 3u, options));
    }
    ExpectSameCraters(whole, joined);
}

TEST(GenerateCraters, LoweringDMinKeepsLargerBandsInPlace) {
    const auto density = terraingen::testing::LunarCraterDensity(3.5);
    const auto ages = terraingen::testing::LunarCraterAges(3.5);
    const Bounds2d region{0.0, 0.0, 400.0, 400.0};
    const CraterList coarse =
        GenerateCraters(kCells, region, *density, *ages, 11u, {.dMin = 3.0, .dMax = 100.0});
    const CraterList fine =
        GenerateCraters(kCells, region, *density, *ages, 11u, {.dMin = 2.5, .dMax = 100.0});
    ExpectSameCraters(Filter(coarse, region, 4.0), Filter(fine, region, 4.0));
    EXPECT_GT(fine.Size(), coarse.Size());
}

TEST(GenerateCraters, ChangingTheAgeModelNeverMovesACrater) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 300.0, 300.0};
    const CraterGenerationOptions options{.dMin = 2.0, .dMax = 32.0};
    const CraterList fresh = GenerateCraters(kCells, region, density, FreshAges(), 5u, options);
    const CraterList aged = GenerateCraters(kCells, region, density, UniformAges(2.0), 5u, options);
    EXPECT_EQ(fresh.Ids(), aged.Ids());
    EXPECT_EQ(fresh.X(), aged.X());
    EXPECT_EQ(fresh.Diameters(), aged.Diameters());
    EXPECT_TRUE(std::any_of(aged.AgesGyr().begin(), aged.AgesGyr().end(),
                            [](double age) { return age > 0.0; }));
    EXPECT_TRUE(std::all_of(aged.AgesGyr().begin(), aged.AgesGyr().end(),
                            [](double age) { return age >= 0.0 && age <= 2.0; }));
}

TEST(GenerateCraters, MarginKeepsCratersReachingIntoTheRegion) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 200.0, 200.0};
    const CraterList inside =
        GenerateCraters(kCells, region, density, FreshAges(), 9u, {.dMin = 4.0, .dMax = 64.0});
    const CraterList withMargin = GenerateCraters(kCells, region, density, FreshAges(), 9u,
                                                  {.dMin = 4.0, .dMax = 64.0, .marginRadii = 3.0});
    ExpectSameCraters(inside, Filter(withMargin, region));
    std::size_t outside = 0;
    for (std::size_t n = 0; n < withMargin.Size(); ++n) {
        const Crater crater = withMargin.At(n);
        EXPECT_TRUE(region.Expanded(1.5 * crater.diameter).Contains(crater.x, crater.y));
        outside += region.Contains(crater.x, crater.y) ? 0u : 1u;
    }
    EXPECT_GT(outside, 0u);
}

TEST(GenerateCraters, UsesTheDensitysRangeForAnInfiniteDMax) {
    const PowerLawSfd bounded(0.1, 2.0, SfdKind::Density, {0.0, 20.0});
    const CraterList craters =
        GenerateCraters(kCells, {0.0, 0.0, 300.0, 300.0}, bounded, FreshAges(), 2u, {.dMin = 5.0});
    ASSERT_FALSE(craters.Empty());
    EXPECT_LE(*std::max_element(craters.Diameters().begin(), craters.Diameters().end()), 20.0);
}

TEST(GenerateCraters, RejectsInvalidRequests) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 100.0, 100.0};
    const FreshAges fresh;
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    const auto generate = [&](const CraterGenerationOptions& options) {
        return GenerateCraters(kCells, region, density, fresh, 1u, options);
    };
    EXPECT_THROW(generate({}), std::invalid_argument);
    EXPECT_THROW(generate({.dMin = kNaN, .dMax = 10.0}), std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 10.0, .dMax = 10.0}), std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 1.0}), std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 1.0, .dMax = 10.0, .marginRadii = -1.0}), std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 1.0, .dMax = 10.0, .cellSizeFactor = 0.0}),
                 std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 1.0, .dMax = 10.0, .maxExpectedCraters = -1.0}),
                 std::invalid_argument);
    EXPECT_THROW(generate({.dMin = 0.01, .dMax = 10.0, .maxExpectedCraters = 1000.0}),
                 std::invalid_argument);
    const PowerLawSfd rate(0.1, 2.0, SfdKind::ProductionRate);
    EXPECT_THROW(GenerateCraters(kCells, region, rate, fresh, 1u, {.dMin = 1.0, .dMax = 10.0}),
                 std::invalid_argument);
    const PowerLawSfd bounded(0.1, 2.0, SfdKind::Density, {0.0, 20.0});
    EXPECT_THROW(GenerateCraters(kCells, region, bounded, fresh, 1u, {.dMin = 30.0}),
                 std::invalid_argument);
    EXPECT_THROW(GenerateCraters(kCells, region, bounded, fresh, 1u, {.dMin = 1.0, .dMax = 40.0}),
                 std::domain_error);
    EXPECT_THROW(GenerateCraters(kCells, {0.0, 0.0, 0.0, 1.0}, density, fresh, 1u,
                                 {.dMin = 1.0, .dMax = 10.0}),
                 std::invalid_argument);
}

TEST(GenerateCraters, LogsASummaryAndEachBand) {
    const terraingen::testing::LogCapture capture(terraingen::LogLevel::Debug);
    const CraterList craters =
        GenerateCraters(kCells, {0.0, 0.0, 200.0, 200.0}, PowerLawSfd(0.1, 2.0), FreshAges(), 3u,
                        {.dMin = 3.0, .dMax = 20.0});
    const auto summary = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(summary.size(), 1u);
    EXPECT_EQ(summary[0].message.rfind(
                  "Generated " + std::to_string(craters.Size()) + " craters (3-20 m, 4 bands", 0),
              0u)
        << summary[0].message;
    EXPECT_EQ(capture.Matching("craters", terraingen::LogLevel::Debug).size(), 4u);
}

namespace {

std::size_t ConflictingPairs(const CraterList& craters, const terraingen::CraterSpacing& spacing) {
    std::size_t pairs = 0;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        for (std::size_t m = n + 1; m < craters.Size(); ++m) {
            pairs += terraingen::CratersConflict(craters.At(n), craters.At(m), spacing) ? 1u : 0u;
        }
    }
    return pairs;
}

}  // namespace

TEST(GenerateCraters, SpacingLeavesNoSimilarCratersTooClose) {
    const terraingen::CraterSpacing spacing{1.5, 1.0};
    const Bounds2d region{0.0, 0.0, 300.0, 300.0};
    const CraterList dense = GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0),
                                             UniformAges(1.0), 4u, {.dMin = 4.0, .dMax = 64.0});
    const terraingen::testing::LogCapture capture;
    const CraterList spaced =
        GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), UniformAges(1.0), 4u,
                        {.dMin = 4.0, .dMax = 64.0, .spacing = spacing});
    EXPECT_GT(ConflictingPairs(dense, spacing), 0u);
    EXPECT_EQ(ConflictingPairs(spaced, spacing), 0u);
    EXPECT_LT(spaced.Size(), dense.Size());
    const auto records = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_NE(records[0].message.find(" removed by spacing"), std::string::npos)
        << records[0].message;
}

TEST(GenerateCraters, SpacedTilesJoinToTheWholeRegion) {
    const terraingen::CraterSpacing spacing{2.0, 0.75};
    const CraterGenerationOptions options{
        .dMin = 2.0, .dMax = 40.0, .marginRadii = 1.5, .spacing = spacing};
    const PowerLawSfd density(0.1, 2.0);
    const UniformAges ages(1.0);
    const CraterList whole =
        GenerateCraters(kCells, {-200.0, -100.0, 200.0, 100.0}, density, ages, 6u, options);
    CraterList joined;
    for (const Bounds2d& tile :
         {Bounds2d{-200.0, -100.0, 13.0, 0.0}, Bounds2d{13.0, -100.0, 200.0, 0.0},
          Bounds2d{-200.0, 0.0, 13.0, 100.0}, Bounds2d{13.0, 0.0, 200.0, 100.0}}) {
        const CraterList part = GenerateCraters(kCells, tile, density, ages, 6u, options);
        for (std::size_t n = 0; n < part.Size(); ++n) {
            if (tile.Contains(part.At(n).x, part.At(n).y)) {
                joined.Append(part.At(n));
            }
        }
    }
    ExpectSameCraters(Filter(whole, {-200.0, -100.0, 200.0, 100.0}), joined);
}

TEST(GenerateCraters, RejectsInvalidSpacing) {
    EXPECT_THROW(GenerateCraters(kCells, {0.0, 0.0, 10.0, 10.0}, PowerLawSfd(0.1, 2.0), FreshAges(),
                                 1u, {.dMin = 1.0, .dMax = 5.0, .spacing = {0.5, 1.0}}),
                 std::invalid_argument);
}

TEST(CraterList, StoresTheManualFlag) {
    CraterList list;
    list.Append({1.0, 2.0, 3.0, 0.5, 7u, true});
    list.Append({4.0, 5.0, 6.0, 1.5, 9u});
    EXPECT_EQ(list.Manual(), (std::vector<std::uint8_t>{1, 0}));
    EXPECT_TRUE(list.At(0).manual);
    EXPECT_FALSE(list.At(1).manual);
    CraterList joined;
    joined.Append(list);
    EXPECT_EQ(joined.Manual(), list.Manual());
}

TEST(ManualCraterId, IsStableAndDependsOnEveryField) {
    const std::uint64_t id = terraingen::ManualCraterId(10.0, 20.0, 30.0);
    EXPECT_EQ(id, terraingen::ManualCraterId(10.0, 20.0, 30.0));
    EXPECT_NE(id, terraingen::ManualCraterId(10.5, 20.0, 30.0));
    EXPECT_NE(id, terraingen::ManualCraterId(10.0, 20.5, 30.0));
    EXPECT_NE(id, terraingen::ManualCraterId(10.0, 20.0, 30.5));
}

TEST(GenerateCraters, MergesManualCratersInsideTheRegion) {
    CraterList manual;
    manual.Append({50.0, 50.0, 30.0, 0.2, 0u});
    manual.Append({60.0, 60.0, 20.0, 0.1, 42u, true});
    manual.Append({900.0, 900.0, 30.0, 0.0, 0u});
    const Bounds2d region{0.0, 0.0, 200.0, 200.0};
    const CraterList plain = GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), FreshAges(), 3u,
                                             {.dMin = 4.0, .dMax = 64.0});
    const CraterList merged = GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), FreshAges(),
                                              3u, {.dMin = 4.0, .dMax = 64.0, .manual = manual});
    ASSERT_EQ(merged.Size(), plain.Size() + 2);
    for (std::size_t n = 0; n < plain.Size(); ++n) {
        EXPECT_EQ(merged.At(n), plain.At(n));
    }
    const Crater first = merged.At(plain.Size());
    EXPECT_TRUE(first.manual);
    EXPECT_EQ(first.ageGyr, 0.2);
    EXPECT_EQ(first.id, terraingen::ManualCraterId(50.0, 50.0, 30.0));
    EXPECT_EQ(merged.At(plain.Size() + 1).id, 42u);
}

TEST(GenerateCraters, ManualCratersClearConflictingGeneratedCraters) {
    const terraingen::CraterSpacing spacing{2.0, 1.0};
    CraterList manual;
    manual.Append({100.0, 100.0, 40.0, 3.0, 0u});
    const Bounds2d region{0.0, 0.0, 200.0, 200.0};
    const CraterList craters =
        GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), UniformAges(1.0), 8u,
                        {.dMin = 8.0, .dMax = 64.0, .spacing = spacing, .manual = manual});
    std::size_t manualCount = 0;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        manualCount += crater.manual ? 1u : 0u;
        if (!crater.manual) {
            EXPECT_FALSE(terraingen::CratersConflict(crater, manual.At(0), spacing)) << n;
        }
    }
    EXPECT_EQ(manualCount, 1u);
    EXPECT_EQ(ConflictingPairs(craters, spacing), 0u);
}

TEST(GenerateCraters, ManualCratersKeepTilesJoined) {
    CraterList manual;
    manual.Append({12.0, 1.0, 30.0, 0.0, 0u});
    manual.Append({-150.0, 50.0, 12.0, 0.0, 0u});
    const CraterGenerationOptions options{
        .dMin = 4.0, .dMax = 40.0, .marginRadii = 1.5, .spacing = {1.5, 0.75}, .manual = manual};
    const PowerLawSfd density(0.1, 2.0);
    const UniformAges ages(1.0);
    const CraterList whole =
        GenerateCraters(kCells, {-200.0, -100.0, 200.0, 100.0}, density, ages, 2u, options);
    CraterList joined;
    for (const Bounds2d& tile :
         {Bounds2d{-200.0, -100.0, 13.0, 0.0}, Bounds2d{13.0, -100.0, 200.0, 0.0},
          Bounds2d{-200.0, 0.0, 13.0, 100.0}, Bounds2d{13.0, 0.0, 200.0, 100.0}}) {
        const CraterList part = GenerateCraters(kCells, tile, density, ages, 2u, options);
        for (std::size_t n = 0; n < part.Size(); ++n) {
            if (tile.Contains(part.At(n).x, part.At(n).y)) {
                joined.Append(part.At(n));
            }
        }
    }
    ExpectSameCraters(Filter(whole, {-200.0, -100.0, 200.0, 100.0}), joined);
}

TEST(GenerateCraters, RejectsInvalidManualCraters) {
    for (const Crater& bad : {Crater{0.0, 0.0, 0.0, 0.0, 0u}, Crater{0.0, 0.0, 5.0, -1.0, 0u},
                              Crater{std::numeric_limits<double>::infinity(), 0.0, 5.0, 0.0, 0u}}) {
        CraterList manual;
        manual.Append(bad);
        EXPECT_THROW(GenerateCraters(kCells, {0.0, 0.0, 10.0, 10.0}, PowerLawSfd(0.1, 2.0),
                                     FreshAges(), 1u, {.dMin = 1.0, .dMax = 5.0, .manual = manual}),
                     std::invalid_argument);
    }
}

TEST(GenerateCraters, MaxCratersKeepsTheLargestGeneratedCraters) {
    const Bounds2d region{0.0, 0.0, 300.0, 300.0};
    CraterList manual;
    manual.Append({10.0, 10.0, 3.0, 0.0, 0u});
    const CraterList all = GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), FreshAges(), 5u,
                                           {.dMin = 2.0, .dMax = 64.0});
    const terraingen::testing::LogCapture capture;
    const CraterList capped =
        GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), FreshAges(), 5u,
                        {.dMin = 2.0, .dMax = 64.0, .manual = manual, .maxCraters = 50});
    ASSERT_GT(all.Size(), 50u);
    ASSERT_EQ(capped.Size(), 51u);
    std::vector<double> diameters(all.Diameters());
    std::sort(diameters.begin(), diameters.end(), std::greater<>());
    double smallestKept = 1e9;
    std::size_t manualKept = 0;
    for (std::size_t n = 0; n < capped.Size(); ++n) {
        if (capped.At(n).manual) {
            ++manualKept;
        } else {
            smallestKept = std::min(smallestKept, capped.At(n).diameter);
        }
    }
    EXPECT_EQ(manualKept, 1u);
    EXPECT_EQ(smallestKept, diameters[49]);
    const auto records = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_NE(records[0].message.find(" over maxCraters dropped"), std::string::npos);
    const CraterList generous =
        GenerateCraters(kCells, region, PowerLawSfd(0.1, 2.0), FreshAges(), 5u,
                        {.dMin = 2.0, .dMax = 64.0, .maxCraters = 1000000});
    EXPECT_EQ(generous.Ids(), all.Ids());
}

TEST(MinDiameterForCount, InvertsTheExpectedCount) {
    const PowerLawSfd density(0.1, 2.0);
    const double area = 1e6;
    const double d = terraingen::MinDiameterForCount(density, area, 500.0, 1.0, 100.0);
    EXPECT_NEAR(density.ExpectedCount(area, d, 100.0), 500.0, 1e-6);
    EXPECT_NEAR(d, 1.0 / std::sqrt(500.0 / (area * 0.1) + 1e-4), 1e-9);
    EXPECT_EQ(terraingen::MinDiameterForCount(density, area, 1e12, 1.0, 100.0), 1.0);
    EXPECT_THROW(terraingen::MinDiameterForCount(density, area, 0.0, 1.0, 100.0),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::MinDiameterForCount(density, -1.0, 5.0, 1.0, 100.0),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::MinDiameterForCount(density, area, 5.0, 10.0, 1.0),
                 std::invalid_argument);
}

TEST(GenerateCraters, CellCraterSourcesGiveTheSameCraters) {
    const terraingen::PowerLawSfd density(0.05, 2.0);
    const terraingen::UniformAges ages(1.0);
    const terraingen::Bounds2d region{-120.0, -80.0, 140.0, 90.0};
    for (const terraingen::CraterSpacing spacing :
         {terraingen::CraterSpacing{}, terraingen::CraterSpacing{1.5, 1.0}}) {
        terraingen::CraterGenerationOptions options{.dMin = 3.0, .dMax = 60.0, .marginRadii = 1.5};
        options.spacing = spacing;
        const auto direct = terraingen::GenerateCraters(terraingen::PlanarCells(), region, density,
                                                        ages, 5u, options);
        int calls = 0;
        options.cellCraters = [&](const terraingen::CraterBand& band,
                                  const terraingen::CellIndex& cell) {
            ++calls;
            return std::make_shared<const terraingen::CraterList>(terraingen::GenerateCellCraters(
                terraingen::PlanarCells(), band, cell, density, ages, 5u));
        };
        const auto supplied = terraingen::GenerateCraters(terraingen::PlanarCells(), region,
                                                          density, ages, 5u, options);
        EXPECT_GT(calls, 0);
        EXPECT_EQ(supplied.Ids(), direct.Ids());
        EXPECT_EQ(supplied.Diameters(), direct.Diameters());
    }
}

TEST(GenerateCraters, SkippingUnresolvedBandsKeepsEveryResolvedCrater) {
    const terraingen::PowerLawSfd density(0.05, 2.0);
    const terraingen::UniformAges ages(1.0);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    for (const terraingen::CraterSpacing spacing :
         {terraingen::CraterSpacing{}, terraingen::CraterSpacing{1.5, 2.0}}) {
        terraingen::CraterGenerationOptions options{.dMin = 2.0, .dMax = 100.0, .marginRadii = 1.5};
        options.spacing = spacing;
        const auto all = terraingen::GenerateCraters(terraingen::PlanarCells(), region, density,
                                                     ages, 9u, options);
        options.minResolvedDiameter = 13.0;
        const auto resolved = terraingen::GenerateCraters(terraingen::PlanarCells(), region,
                                                          density, ages, 9u, options);
        EXPECT_LT(resolved.Size(), all.Size());
        std::vector<std::uint64_t> expected;
        std::vector<std::uint64_t> kept;
        for (std::size_t n = 0; n < all.Size(); ++n) {
            if (all.At(n).diameter >= 13.0) {
                expected.push_back(all.At(n).id);
            }
        }
        for (std::size_t n = 0; n < resolved.Size(); ++n) {
            if (resolved.At(n).diameter >= 13.0) {
                kept.push_back(resolved.At(n).id);
            }
        }
        ASSERT_FALSE(expected.empty());
        EXPECT_EQ(kept, expected) << "spacing " << spacing.separation;
    }
    terraingen::CraterGenerationOptions bad{.dMin = 2.0, .dMax = 100.0};
    bad.minResolvedDiameter = -1.0;
    EXPECT_THROW(
        terraingen::GenerateCraters(terraingen::PlanarCells(), region, density, ages, 9u, bad),
        std::invalid_argument);
}

TEST(ResolveDMax, FallsBackToTheTopOfTheDensitysRange) {
    const terraingen::PowerLawSfd unbounded(0.05, 2.0);
    EXPECT_EQ(terraingen::ResolveDMax(50.0, unbounded), 50.0);
    EXPECT_TRUE(std::isinf(terraingen::ResolveDMax(INFINITY, unbounded)));
    const terraingen::PowerLawSfd bounded(0.05, 2.0, terraingen::SfdKind::Density, {1.0, 300.0});
    EXPECT_EQ(terraingen::ResolveDMax(INFINITY, bounded), 300.0);
}

TEST(FilterCraters, KeepsWhatThePredicateAcceptsInOrder) {
    CraterList craters;
    for (std::uint64_t id = 1; id <= 5; ++id) {
        craters.Append({0.0, 0.0, static_cast<double>(id), 0.0, id});
    }
    const CraterList kept =
        terraingen::FilterCraters(craters, [](std::size_t n, const terraingen::Crater& crater) {
            return n != 0 && crater.diameter < 5.0;
        });
    EXPECT_EQ(kept.Ids(), (std::vector<std::uint64_t>{2, 3, 4}));
}
