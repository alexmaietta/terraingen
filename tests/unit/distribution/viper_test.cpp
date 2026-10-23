#include "terraingen/distribution/viper.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(ViperSpecification, JumpsBetweenItsTwoPowerLawsAtEightyMeters) {
    const auto viper = terraingen::ViperSpecification();
    EXPECT_EQ(viper.Kind(), terraingen::SfdKind::Density);
    EXPECT_DOUBLE_EQ(viper.Cumulative(10.0), 29174.0 / 1e6 * std::pow(10.0, -1.92));
    EXPECT_DOUBLE_EQ(viper.Cumulative(100.0), 156228.0 / 1e6 * std::pow(100.0, -2.389));
}
