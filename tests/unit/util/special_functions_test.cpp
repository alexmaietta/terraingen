// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/special_functions.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::ExponentialIntegral;

TEST(ExponentialIntegral, MatchesTabulatedValues) {
    EXPECT_NEAR(ExponentialIntegral(1, 1.0), 0.21938393439552029, 1e-15);
    EXPECT_NEAR(ExponentialIntegral(1, 0.1), 1.8229239584193906, 1e-14);
    EXPECT_NEAR(ExponentialIntegral(2, 1.0), 0.14849550677592205, 1e-15);
    EXPECT_NEAR(ExponentialIntegral(2, 0.01), 0.94967053798378715, 1e-14);
    EXPECT_NEAR(ExponentialIntegral(2, 20.0), 9.4048564308581544e-11, 1e-24);
    EXPECT_EQ(ExponentialIntegral(2, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(ExponentialIntegral(0, 2.0), std::exp(-2.0) / 2.0);
    EXPECT_NEAR(ExponentialIntegral(3, 0.5), 0.22160436427517818, 1e-15);
    EXPECT_THROW(ExponentialIntegral(1, 0.0), std::domain_error);
    EXPECT_THROW(ExponentialIntegral(-1, 1.0), std::domain_error);
    EXPECT_THROW(ExponentialIntegral(2, -1.0), std::domain_error);
}
