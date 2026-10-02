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

TEST(FreshnessDegradation, ScalesDepthAndRimByOneErosionFactor) {
    const FreshnessDegradation degradation(std::make_shared<UniformAges>(2.0));
    const auto at = [&](double ageGyr) {
        return degradation.State(Crater{0.0, 0.0, 150.0, ageGyr, 1u});
    };
    const CraterState fresh = at(0.0);
    EXPECT_DOUBLE_EQ(fresh.current.depthRatio, 0.15);
    EXPECT_DOUBLE_EQ(fresh.current.rimHeightRatio, kFreshRimHeightRatio);
    const CraterState worn = at(2.0);
    EXPECT_DOUBLE_EQ(worn.current.depthRatio, 0.04);
    EXPECT_DOUBLE_EQ(worn.current.rimHeightRatio, kFreshRimHeightRatio * 0.04 / 0.15);
    EXPECT_EQ(worn.fresh.depthRatio, 0.15);
    const CraterState half = at(1.0);
    EXPECT_DOUBLE_EQ(half.current.depthRatio, 0.5 * (0.15 + 0.04));
    EXPECT_DOUBLE_EQ(half.current.depthRatio / half.fresh.depthRatio,
                     half.current.rimHeightRatio / half.fresh.rimHeightRatio);
    EXPECT_THROW(FreshnessDegradation(nullptr), std::invalid_argument);
    EXPECT_THROW(FreshnessDegradation(std::make_shared<UniformAges>(1.0), 0.0),
                 std::invalid_argument);
    EXPECT_THROW(FreshnessDegradation(std::make_shared<UniformAges>(1.0), 0.1),
                 std::invalid_argument);
}
