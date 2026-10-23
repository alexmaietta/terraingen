#include "terraingen/distribution/ivanov.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

TEST(IvanovMarsChronology, FollowsIvanov2001) {
    EXPECT_EQ(terraingen::IvanovMarsChronology(0.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::IvanovMarsChronology(3.0),
                     (2.68e-14 * std::expm1(6.93 * 3.0) + 4.13e-4 * 3.0) / 1e6);
    EXPECT_DOUBLE_EQ(terraingen::IvanovMarsChronologyRate(0.0), (2.68e-14 * 6.93 + 4.13e-4) / 1e6);
    const double h = 1e-6;
    EXPECT_NEAR(
        terraingen::IvanovMarsChronologyRate(2.0),
        (terraingen::IvanovMarsChronology(2.0 + h) - terraingen::IvanovMarsChronology(2.0 - h)) /
            (2.0 * h),
        1e-6 * terraingen::IvanovMarsChronologyRate(2.0));
    EXPECT_THROW(terraingen::IvanovMarsChronology(-0.1), std::domain_error);
    EXPECT_THROW(terraingen::IvanovMarsChronologyRate(5.0), std::domain_error);
}

TEST(IvanovMarsProductionFunction, MatchesItsPresentDayRateAtOneKilometer) {
    const auto production = terraingen::IvanovMarsProductionFunction();
    EXPECT_EQ(production.Kind(), terraingen::SfdKind::ProductionRate);
    EXPECT_EQ(production.ValidRange().min, 15.0);
    EXPECT_EQ(production.ValidRange().max, 362000.0);
    EXPECT_NEAR(production.Cumulative(1000.0) * 1e6, std::pow(10.0, -3.383677), 1e-12);
    EXPECT_NEAR(production.Cumulative(1000.0) * 1e6, 4.13e-4, 0.01 * 4.13e-4)
        << "a_0 is the chronology's linear term";
    EXPECT_GT(production.Cumulative(100.0), production.Cumulative(1000.0));
    EXPECT_THROW(production.Cumulative(10.0), std::domain_error);
}
