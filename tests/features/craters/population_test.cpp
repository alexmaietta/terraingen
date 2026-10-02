#include "support/log_capture.h"
#include "terraingen/craters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::AllFresh;
using terraingen::Bounds2d;
using terraingen::Crater;
using terraingen::CraterGenerationOptions;
using terraingen::CraterList;
using terraingen::GenerateCraters;
using terraingen::LunarSurface;
using terraingen::PlanarCells;
using terraingen::PowerLaw;
using terraingen::SfdKind;
using terraingen::SurfaceModel;
using terraingen::UniformFreshness;

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
    const PowerLaw density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 500.0, 500.0};
    const CraterGenerationOptions options{.dMin = 2.0, .dMax = 64.0};
    const CraterList first = GenerateCraters(kCells, region, density, AllFresh(), 7u, options);
    const CraterList second = GenerateCraters(kCells, region, density, AllFresh(), 7u, options);
    const CraterList other = GenerateCraters(kCells, region, density, AllFresh(), 8u, options);
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
    const PowerLaw density(0.1, 2.0);
    const CraterGenerationOptions options{.dMin = 1.0, .dMax = 50.0};
    const CraterList whole =
        GenerateCraters(kCells, {-300.0, -200.0, 300.0, 200.0}, density, AllFresh(), 3u, options);
    CraterList joined;
    for (const Bounds2d& tile :
         {Bounds2d{-300.0, -200.0, 17.5, 0.0}, Bounds2d{17.5, -200.0, 300.0, 0.0},
          Bounds2d{-300.0, 0.0, 17.5, 200.0}, Bounds2d{17.5, 0.0, 300.0, 200.0}}) {
        joined.Append(GenerateCraters(kCells, tile, density, AllFresh(), 3u, options));
    }
    ExpectSameCraters(whole, joined);
}

TEST(GenerateCraters, LoweringDMinKeepsLargerBandsInPlace) {
    const SurfaceModel surface = LunarSurface(3.5);
    const Bounds2d region{0.0, 0.0, 400.0, 400.0};
    const CraterList coarse = GenerateCraters(kCells, region, *surface.density, *surface.ages, 11u,
                                              {.dMin = 3.0, .dMax = 100.0});
    const CraterList fine = GenerateCraters(kCells, region, *surface.density, *surface.ages, 11u,
                                            {.dMin = 2.5, .dMax = 100.0});
    ExpectSameCraters(Filter(coarse, region, 4.0), Filter(fine, region, 4.0));
    EXPECT_GT(fine.Size(), coarse.Size());
}

TEST(GenerateCraters, ChangingTheAgeModelNeverMovesACrater) {
    const PowerLaw density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 300.0, 300.0};
    const CraterGenerationOptions options{.dMin = 2.0, .dMax = 32.0};
    const CraterList fresh = GenerateCraters(kCells, region, density, AllFresh(), 5u, options);
    const CraterList aged =
        GenerateCraters(kCells, region, density, UniformFreshness(2.0), 5u, options);
    EXPECT_EQ(fresh.Ids(), aged.Ids());
    EXPECT_EQ(fresh.X(), aged.X());
    EXPECT_EQ(fresh.Diameters(), aged.Diameters());
    EXPECT_TRUE(std::any_of(aged.AgesGyr().begin(), aged.AgesGyr().end(),
                            [](double age) { return age > 0.0; }));
    EXPECT_TRUE(std::all_of(aged.AgesGyr().begin(), aged.AgesGyr().end(),
                            [](double age) { return age >= 0.0 && age <= 2.0; }));
}

TEST(GenerateCraters, MarginKeepsCratersReachingIntoTheRegion) {
    const PowerLaw density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 200.0, 200.0};
    const CraterList inside =
        GenerateCraters(kCells, region, density, AllFresh(), 9u, {.dMin = 4.0, .dMax = 64.0});
    const CraterList withMargin = GenerateCraters(kCells, region, density, AllFresh(), 9u,
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
    const PowerLaw bounded(0.1, 2.0, SfdKind::Density, {0.0, 20.0});
    const CraterList craters =
        GenerateCraters(kCells, {0.0, 0.0, 300.0, 300.0}, bounded, AllFresh(), 2u, {.dMin = 5.0});
    ASSERT_FALSE(craters.Empty());
    EXPECT_LE(*std::max_element(craters.Diameters().begin(), craters.Diameters().end()), 20.0);
}

TEST(GenerateCraters, RejectsInvalidRequests) {
    const PowerLaw density(0.1, 2.0);
    const Bounds2d region{0.0, 0.0, 100.0, 100.0};
    const AllFresh fresh;
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
    const PowerLaw rate(0.1, 2.0, SfdKind::ProductionRate);
    EXPECT_THROW(GenerateCraters(kCells, region, rate, fresh, 1u, {.dMin = 1.0, .dMax = 10.0}),
                 std::invalid_argument);
    const PowerLaw bounded(0.1, 2.0, SfdKind::Density, {0.0, 20.0});
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
    const CraterList craters = GenerateCraters(kCells, {0.0, 0.0, 200.0, 200.0}, PowerLaw(0.1, 2.0),
                                               AllFresh(), 3u, {.dMin = 3.0, .dMax = 20.0});
    const auto summary = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(summary.size(), 1u);
    EXPECT_EQ(summary[0].message.rfind(
                  "Generated " + std::to_string(craters.Size()) + " craters (3-20 m, 4 bands", 0),
              0u)
        << summary[0].message;
    EXPECT_EQ(capture.Matching("craters", terraingen::LogLevel::Debug).size(), 4u);
}
