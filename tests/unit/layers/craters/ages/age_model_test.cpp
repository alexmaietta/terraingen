// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/ages/age_model.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::CraterAgeModel;
using terraingen::FreshAges;
using terraingen::RandomStream;
using terraingen::UniformAges;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

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
