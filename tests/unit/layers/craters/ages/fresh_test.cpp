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

}  // namespace

TEST(FreshAges, GivesEveryCraterAgeZeroAndFreshnessOne) {
    const FreshAges fresh;
    RandomStream stream(1u, 1u);
    EXPECT_EQ(fresh.LifetimeGyr(10.0), 0.0);
    EXPECT_EQ(fresh.SampleAgeGyr(stream, 10.0), 0.0);
    EXPECT_EQ(fresh.RemainingLifetime(10.0, 0.0), 1.0);
    EXPECT_EQ(fresh.RemainingLifetime(10.0, 2.0), 1.0);
    EXPECT_THROW(fresh.LifetimeGyr(0.0), std::domain_error);
    EXPECT_THROW(fresh.SampleAgeGyr(stream, kNaN), std::domain_error);
    EXPECT_THROW(fresh.RemainingLifetime(-1.0, 0.0), std::domain_error);
    const std::unique_ptr<CraterAgeModel> base = std::make_unique<FreshAges>();
    EXPECT_EQ(base->LifetimeGyr(1.0), 0.0);
}
