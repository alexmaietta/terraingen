#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::DiameterRange;
using terraingen::LogPolynomialSfd;
using terraingen::MinimumSfd;
using terraingen::PiecewisePowerLawSfd;
using terraingen::PowerLawCoefficientFromKilometres;
using terraingen::PowerLawSegment;
using terraingen::PowerLawSfd;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SizeFrequencyDistribution;
using terraingen::SplicedSfd;
using terraingen::TabulatedSfd;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST(TabulatedSfd, InterpolatesPowerLawsBetweenPoints) {
    const PowerLawSfd law(2.0, 2.0);
    const TabulatedSfd table({1.0, 10.0, 100.0}, {2.0, 0.02, 0.0002});
    for (const double d : {1.0, 3.0, 10.0, 42.0, 100.0}) {
        EXPECT_NEAR(table.Cumulative(d), law.Cumulative(d), 1e-12 * law.Cumulative(d)) << d;
        EXPECT_NEAR(table.Incremental(d), law.Incremental(d), 1e-12 * law.Incremental(d)) << d;
    }
    RandomStream tableStream(2u, 3u);
    RandomStream lawStream(2u, 3u);
    for (int i = 0; i < 50; ++i) {
        const double expected = law.SampleDiameter(lawStream, 1.5, 80.0);
        EXPECT_NEAR(table.SampleDiameter(tableStream, 1.5, 80.0), expected, 1e-9 * expected);
    }
    EXPECT_EQ(table.ValidRange().min, 1.0);
    EXPECT_EQ(table.ValidRange().max, 100.0);
    EXPECT_THROW(table.Cumulative(0.5), std::domain_error);
}

TEST(TabulatedSfd, UsesEachIntervalsSlopeAndConvertsUnits) {
    const TabulatedSfd table({0.001, 0.01, 0.1}, {1e6, 1e5, 1e2}, SfdKind::ProductionRate, 1000.0,
                             1e6);
    EXPECT_EQ(table.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(table.Diameters().back(), 100.0);
    EXPECT_DOUBLE_EQ(table.CumulativeValues().front(), 1.0);
    EXPECT_NEAR(table.Cumulative(std::sqrt(10.0)), std::pow(10.0, -0.5), 1e-12);
    EXPECT_NEAR(table.Cumulative(std::sqrt(1000.0)), 1e-1 * std::pow(10.0, -1.5), 1e-12);
    EXPECT_NEAR(table.Incremental(5.0), table.Cumulative(5.0) / 5.0, 1e-12);
    EXPECT_NEAR(table.Incremental(50.0), 3.0 * table.Cumulative(50.0) / 50.0, 1e-12);
}

TEST(TabulatedSfd, FlatIntervalsHoldNoMass) {
    const TabulatedSfd table({1.0, 2.0, 4.0}, {1.0, 1.0, 0.25});
    EXPECT_EQ(table.Incremental(1.5), 0.0);
    RandomStream stream(6u, 6u);
    for (int i = 0; i < 200; ++i) {
        EXPECT_GE(table.SampleDiameter(stream, 1.0, 4.0), 2.0);
    }
}

TEST(TabulatedSfd, ExtrapolatesTheEndSlopesByOptIn) {
    TabulatedSfd table({1.0, 10.0, 100.0}, {1.0, 0.1, 0.0001});
    table.SetAllowExtrapolation(true);
    EXPECT_NEAR(table.Cumulative(0.1), 10.0, 1e-12);
    EXPECT_NEAR(table.Cumulative(1000.0), 1e-7, 1e-18);
    RandomStream stream(3u, 9u);
    for (int i = 0; i < 50; ++i) {
        const double d = table.SampleDiameter(stream, 0.1, 1000.0);
        EXPECT_GE(d, 0.1);
        EXPECT_LE(d, 1000.0);
    }
    const PowerLawSfd steep(1e-1 * 1000.0, 3.0);
    RandomStream tableStream(4u, 4u);
    RandomStream lawStream(4u, 4u);
    for (int i = 0; i < 20; ++i) {
        const double expected = steep.SampleDiameter(lawStream, 200.0, 2000.0);
        EXPECT_NEAR(table.SampleDiameter(tableStream, 200.0, 2000.0), expected, 1e-9 * expected);
    }
}

TEST(TabulatedSfd, RejectsMalformedTables) {
    EXPECT_THROW(TabulatedSfd({1.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({2.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({0.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, kInf}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {kNaN, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {0.5, 1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.5}, SfdKind::Density, 0.0),
                 std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.5}, SfdKind::Density, 1.0, kNaN),
                 std::invalid_argument);
}
