#include "support/log_capture.h"
#include "terraingen/craters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::ApplyCraters;
using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterList;
using terraingen::CraterOrder;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::GeoTransform;
using terraingen::NoDegradation;
using terraingen::Raster;

namespace {

/// A `size` x `size` north-up raster of 1 m pixels centred on the origin.
Raster<float> Flat(std::size_t size, float value = 0.0f) {
    Raster<float> raster(size, size, value);
    const double half = 0.5 * static_cast<double>(size);
    raster.SetTransform(GeoTransform::NorthUp(-half, half, 1.0));
    return raster;
}

CraterList One(double x, double y, double diameter, double age = 0.0, std::uint64_t id = 1) {
    CraterList craters;
    craters.Append({x, y, diameter, age, id});
    return craters;
}

/// Height at the cell whose centre is nearest (x, y).
float At(const Raster<float>& raster, double x, double y) {
    const auto pixel = raster.Transform().WorldToPixel(x, y);
    return raster(static_cast<std::size_t>(pixel.x), static_cast<std::size_t>(pixel.y));
}

/// Profile height of a fresh crater `diameter` across at `distance` from its centre.
double Fresh(const FassettThomsonProfile& profile, double distance, double diameter) {
    const CraterState state = NoDegradation().State(Crater{0.0, 0.0, diameter, 0.0, 0u});
    return profile.Height(distance, diameter, state);
}

}  // namespace

TEST(ApplyCraters, StampsTheProfileOnFlatTerrain) {
    Raster<float> raster = Flat(100);
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.5, 0.5, 40.0), profile);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const auto centre = raster.CellCenter(col, row);
            const double r = std::hypot(centre.x - 0.5, centre.y - 0.5);
            EXPECT_NEAR(raster(col, row), Fresh(profile, r, 40.0), 1e-5) << col << " " << row;
        }
    }
    EXPECT_NEAR(At(raster, 0.5, 0.5), -(0.13 - terraingen::kFreshRimHeightRatio) * 40.0, 1e-5);
}

TEST(ApplyCraters, KeepsTheRegionalSlope) {
    Raster<float> raster = Flat(100);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = static_cast<float>(0.2 * raster.CellCenter(col, row).x + 5.0);
        }
    }
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(10.5, -4.5, 30.0), profile);
    for (const double dx : {0.0, 5.0, -9.0, 20.0}) {
        const double x = 10.5 + dx;
        const double expected = 0.2 * x + 5.0 + Fresh(profile, std::fabs(dx), 30.0);
        EXPECT_NEAR(At(raster, x, -4.5), expected, 1e-4) << dx;
    }
    EXPECT_NEAR(At(raster, -40.5, 40.5), 0.2 * -40.5 + 5.0, 1e-5);
}

TEST(ApplyCraters, ReplacesOlderReliefInsideTheRimAndDrapesEjectaOutside) {
    Raster<float> raster = Flat(80);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = (col + row) % 2 == 0 ? 0.5f : -0.5f;
        }
    }
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.0, 0.0, 40.0), profile);
    const float centre = At(raster, 0.5, 0.5);
    const float neighbour = At(raster, 1.5, 0.5);
    EXPECT_NEAR(centre, neighbour, 0.05);
    const double outside =
        static_cast<double>(At(raster, 26.5, 0.5)) - Fresh(profile, std::hypot(26.5, 0.5), 40.0);
    const double outsideNeighbour =
        static_cast<double>(At(raster, 27.5, 0.5)) - Fresh(profile, std::hypot(27.5, 0.5), 40.0);
    EXPECT_NEAR(std::fabs(outside - outsideNeighbour), 1.0, 1e-5);
}

TEST(ApplyCraters, OrderDecidesWhichCraterOverprints) {
    CraterList craters;
    craters.Append({0.0, 0.0, 50.0, 1.0, 1});
    craters.Append({3.0, 0.0, 10.0, 2.0, 2});
    const FassettThomsonProfile profile;

    Raster<float> big = Flat(100);
    ApplyCraters(big, One(0.0, 0.0, 50.0), profile);

    Raster<float> oldest = Flat(100);
    ApplyCraters(oldest, craters, profile, {.order = CraterOrder::OldestFirst});
    for (const double x : {0.5, 3.5, 6.5, -8.5, 15.5}) {
        EXPECT_NEAR(At(oldest, x, 0.5), At(big, x, 0.5), 0.02) << x;
    }

    Raster<float> largest = Flat(100);
    ApplyCraters(largest, craters, profile, {.order = CraterOrder::LargestFirst});
    EXPECT_LT(At(largest, 3.5, 0.5), At(big, 3.5, 0.5) - 0.5f);
}

TEST(ApplyCraters, BreaksTiesByAgeThenId) {
    const FassettThomsonProfile profile;
    const auto apply = [&](const CraterList& craters) {
        Raster<float> raster = Flat(60);
        ApplyCraters(raster, craters, profile, {.order = CraterOrder::LargestFirst});
        return raster;
    };
    CraterList byAge;
    byAge.Append({-4.0, 0.0, 20.0, 1.0, 1});
    byAge.Append({4.0, 0.0, 20.0, 2.0, 2});
    CraterList byAgeSwapped;
    byAgeSwapped.Append(byAge.At(1));
    byAgeSwapped.Append(byAge.At(0));
    const Raster<float> older = apply(byAge);
    const Raster<float> olderSwapped = apply(byAgeSwapped);
    EXPECT_TRUE(
        std::equal(older.Values().begin(), older.Values().end(), olderSwapped.Values().begin()));
    CraterList byId;
    byId.Append({-4.0, 0.0, 20.0, 1.0, 5});
    byId.Append({4.0, 0.0, 20.0, 1.0, 3});
    CraterList byIdSwapped;
    byIdSwapped.Append(byId.At(1));
    byIdSwapped.Append(byId.At(0));
    const Raster<float> first = apply(byId);
    const Raster<float> second = apply(byIdSwapped);
    EXPECT_TRUE(std::equal(first.Values().begin(), first.Values().end(), second.Values().begin()));
}

TEST(ApplyCraters, SkipsUnresolvedCratersAndCratersOffTheRaster) {
    Raster<float> raster = Flat(50);
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.5, 0.5, 1.5), profile);
    ApplyCraters(raster, One(500.0, 500.0, 20.0), profile);
    ApplyCraters(raster, CraterList(), profile);
    for (const float value : raster.Values()) {
        EXPECT_EQ(value, 0.0f);
    }
    ApplyCraters(raster, One(0.5, 0.5, 1.5), profile, {.minDiameterPixels = 0.5});
    EXPECT_LT(At(raster, 0.5, 0.5), 0.0f);
}

TEST(ApplyCraters, HandlesCratersOverlappingTheEdgeAndNoData) {
    Raster<float> raster = Flat(40);
    raster.SetNoData(-9999.0f);
    raster(20, 20) = -9999.0f;
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(19.5, 0.5, 20.0), profile);
    EXPECT_NEAR(At(raster, 19.5, 0.5), Fresh(profile, 0.0, 20.0), 1e-5);
    ApplyCraters(raster, One(0.5, -0.5, 10.0), profile);
    EXPECT_EQ(raster(20, 20), -9999.0f);
}

TEST(ApplyCraters, RejectsInvalidOptionsAndTransforms) {
    Raster<float> raster = Flat(10);
    const FassettThomsonProfile profile;
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile, {.minDiameterPixels = -1.0}),
                 std::invalid_argument);
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile,
                              {.minDiameterPixels = std::numeric_limits<double>::infinity()}),
                 std::invalid_argument);
    raster.SetTransform(GeoTransform{0.0, 0.0, 0.0, 0.0, 0.0, 1.0});
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile), std::domain_error);
}

TEST(ApplyCraters, LogsAppliedAndSkippedCounts) {
    const terraingen::testing::LogCapture capture;
    Raster<float> raster = Flat(50);
    CraterList craters;
    craters.Append({0.5, 0.5, 10.0, 0.0, 1});
    craters.Append({0.5, 0.5, 1.0, 0.0, 2});
    craters.Append({500.0, 500.0, 10.0, 0.0, 3});
    ApplyCraters(raster, craters, FassettThomsonProfile());
    const auto records = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(
        records[0].message.rfind("Applied 1 of 3 craters (1 below 3 px, 1 off the raster) in ", 0),
        0u)
        << records[0].message;
}
