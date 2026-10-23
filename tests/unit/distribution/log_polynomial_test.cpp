// SPDX-License-Identifier: BSD-3-Clause

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
using terraingen::PowerLawCoefficientFromKilometers;
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

TEST(LogPolynomialSfd, ConvertsUnitsAndMatchesTheEquivalentPowerLaw) {
    const LogPolynomialSfd poly({std::log10(2.0), -2.0}, DiameterRange{1.0, 1e5}, SfdKind::Density,
                                1000.0, 1e6);
    const PowerLawSfd law(2.0, 2.0);
    for (const double d : {1.0, 37.0, 1000.0, 1e5}) {
        EXPECT_NEAR(poly.Cumulative(d), law.Cumulative(d), 1e-12 * law.Cumulative(d)) << d;
        EXPECT_NEAR(poly.Incremental(d), law.Incremental(d), 1e-12 * law.Incremental(d)) << d;
    }
    RandomStream polyStream(4u, 4u);
    RandomStream lawStream(4u, 4u);
    for (int i = 0; i < 50; ++i) {
        const double expected = law.SampleDiameter(lawStream, 3.0, 3000.0);
        EXPECT_NEAR(poly.SampleDiameter(polyStream, 3.0, 3000.0), expected, 1e-9 * expected);
    }
    EXPECT_EQ(poly.Coefficients().size(), 2u);
    EXPECT_THROW(poly.Cumulative(2e5), std::domain_error);
}

TEST(LogPolynomialSfd, RejectsMalformedArgumentsAndIncreasingCurves) {
    const DiameterRange range{1.0, 100.0};
    EXPECT_THROW(LogPolynomialSfd({}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, kNaN}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, range, SfdKind::Density, 0.0),
                 std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, range, SfdKind::Density, 1.0, kInf),
                 std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, DiameterRange{0.0, 100.0}), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, DiameterRange{1.0, kInf}), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, 1.0}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, 0.0, -1.0}, DiameterRange{0.1, 10.0}),
                 std::invalid_argument);
    EXPECT_NO_THROW(LogPolynomialSfd({0.0, 0.0, -1.0}, DiameterRange{1.0, 10.0}));
    EXPECT_DOUBLE_EQ(LogPolynomialSfd({0.5}, range).Cumulative(10.0), std::pow(10.0, 0.5));
}
