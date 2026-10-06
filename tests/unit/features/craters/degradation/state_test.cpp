#include "terraingen/craters.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterState;
using terraingen::DegradationModel;
using terraingen::FreshCraterDepthRatio;
using terraingen::FreshnessDegradation;
using terraingen::kFreshRimHeightRatio;
using terraingen::NoDegradation;
using terraingen::UniformAges;

TEST(FreshCraterDepthRatio, FollowsFassett2022Table2) {
    EXPECT_EQ(FreshCraterDepthRatio(1.0), 0.10);
    EXPECT_EQ(FreshCraterDepthRatio(10.0), 0.10);
    EXPECT_EQ(FreshCraterDepthRatio(10.5), 0.11);
    EXPECT_EQ(FreshCraterDepthRatio(40.0), 0.13);
    EXPECT_EQ(FreshCraterDepthRatio(150.0), 0.15);
    EXPECT_EQ(FreshCraterDepthRatio(399.0), 0.17);
    EXPECT_EQ(FreshCraterDepthRatio(400.0), 0.21);
    EXPECT_EQ(FreshCraterDepthRatio(5000.0), 0.21);
    EXPECT_THROW(FreshCraterDepthRatio(0.0), std::domain_error);
    EXPECT_THROW(FreshCraterDepthRatio(std::numeric_limits<double>::quiet_NaN()),
                 std::domain_error);
}
