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

TEST(PowerLawSfd, EvaluatesItsClosedForm) {
    const PowerLawSfd law(2.0, 2.5, SfdKind::ProductionRate, DiameterRange{1.0, 100.0});
    EXPECT_EQ(law.Coefficient(), 2.0);
    EXPECT_EQ(law.Exponent(), 2.5);
    EXPECT_EQ(law.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(law.Cumulative(4.0), 2.0 * std::pow(4.0, -2.5));
    EXPECT_DOUBLE_EQ(law.Incremental(4.0), 2.0 * 2.5 * std::pow(4.0, -3.5));
}

TEST(PowerLawSfd, RejectsInvalidParameters) {
    EXPECT_THROW(PowerLawSfd(0.0, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, -1.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(kInf, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{-1.0, 10.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{5.0, 5.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, kNaN}),
                 std::invalid_argument);
}

TEST(PowerLawSfd, ConvertsKilometreCoefficientsToMetres) {
    EXPECT_DOUBLE_EQ(PowerLawCoefficientFromKilometres(0.1, 2.0), 0.1);
    EXPECT_DOUBLE_EQ(PowerLawCoefficientFromKilometres(1.0, 3.0), 1000.0);
    const PowerLawSfd metres(PowerLawCoefficientFromKilometres(5.0, 1.8), 1.8);
    EXPECT_NEAR(metres.Cumulative(2000.0) * 1e6, 5.0 * std::pow(2.0, -1.8), 1e-12);
}
