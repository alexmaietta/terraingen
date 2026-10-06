#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::DiameterRange;
using terraingen::ExtendedNeukumProductionFunction;
using terraingen::LogPolynomialSfd;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumLunarChronologyRate;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLawSfd;
using terraingen::ProductionSurface;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SplicedSfd;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST(NeukumLunarChronology, EvaluatesEquationFiveWithinTheLunarAge) {
    EXPECT_EQ(NeukumLunarChronology(0.0), 0.0);
    EXPECT_NEAR(NeukumLunarChronology(1.0) * 1e6, 8.3800006e-4, 1e-11);
    EXPECT_NEAR(NeukumLunarChronology(3.5) * 1e6, 4.792566e-3, 1e-9);
    EXPECT_NO_THROW(NeukumLunarChronology(terraingen::kMaxLunarAgeGyr));
    EXPECT_THROW(NeukumLunarChronology(-0.1), std::domain_error);
    EXPECT_THROW(NeukumLunarChronology(4.7), std::domain_error);
    EXPECT_THROW(NeukumLunarChronology(kNaN), std::domain_error);
}

TEST(NeukumLunarChronology, RateIsTheDerivativeOfTheChronology) {
    EXPECT_NEAR(NeukumLunarChronologyRate(0.0) * 1e6, 8.38e-4, 1e-12);
    for (const double t : {0.5, 3.0, 4.0}) {
        const double step = 1e-6;
        const double numeric =
            (NeukumLunarChronology(t + step) - NeukumLunarChronology(t - step)) / (2.0 * step);
        EXPECT_NEAR(NeukumLunarChronologyRate(t), numeric, 1e-6 * numeric) << t;
    }
    EXPECT_THROW(NeukumLunarChronologyRate(-1.0), std::domain_error);
    EXPECT_THROW(NeukumLunarChronologyRate(5.0), std::domain_error);
}
