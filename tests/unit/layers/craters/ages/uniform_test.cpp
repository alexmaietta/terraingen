#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::CraterAgeModel;
using terraingen::ExtendedNeukumProductionFunction;
using terraingen::FreshAges;
using terraingen::LogPolynomialSfd;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLawSfd;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SteadyStateAges;
using terraingen::TraskEquilibrium;
using terraingen::UniformAges;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST(UniformAges, DrawsAgesUniformlyOverOneLifetime) {
    const UniformAges uniform(2.0);
    RandomStream copy(4u, 2u);
    RandomStream stream(4u, 2u);
    for (int i = 0; i < 50; ++i) {
        const double expected = 2.0 * copy.NextDouble();
        EXPECT_DOUBLE_EQ(uniform.SampleAgeGyr(stream, 1.0 + i), expected);
    }
    EXPECT_EQ(uniform.LifetimeGyr(1e4), 2.0);
    EXPECT_DOUBLE_EQ(uniform.RemainingLifetime(5.0, 0.5), 0.75);
    EXPECT_EQ(uniform.RemainingLifetime(5.0, 3.0), 0.0);
    EXPECT_EQ(uniform.RemainingLifetime(5.0, -1.0), 1.0);
    EXPECT_THROW(UniformAges(0.0), std::invalid_argument);
    EXPECT_THROW(UniformAges{kInf}, std::invalid_argument);
    EXPECT_THROW(UniformAges{kNaN}, std::invalid_argument);
}
