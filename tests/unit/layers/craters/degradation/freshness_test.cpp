#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterState;
using terraingen::FreshnessDegradation;
using terraingen::kLunarFreshRimHeightRatio;
using terraingen::LunarFreshCraterMorphometry;
using terraingen::UniformAges;

TEST(FreshnessDegradation, ScalesDepthAndRimByOneErosionFactor) {
    const FreshnessDegradation degradation(std::make_shared<UniformAges>(2.0),
                                           LunarFreshCraterMorphometry);
    const auto at = [&](double ageGyr) {
        return degradation.State(Crater{0.0, 0.0, 150.0, ageGyr, 1u});
    };
    const CraterState fresh = at(0.0);
    EXPECT_DOUBLE_EQ(fresh.current.depthRatio, 0.15);
    EXPECT_DOUBLE_EQ(fresh.current.rimHeightRatio, kLunarFreshRimHeightRatio);
    const CraterState worn = at(2.0);
    EXPECT_DOUBLE_EQ(worn.current.depthRatio, 0.04);
    EXPECT_DOUBLE_EQ(worn.current.rimHeightRatio, kLunarFreshRimHeightRatio * 0.04 / 0.15);
    EXPECT_EQ(worn.fresh.depthRatio, 0.15);
    const CraterState half = at(1.0);
    EXPECT_DOUBLE_EQ(half.current.depthRatio, 0.5 * (0.15 + 0.04));
    EXPECT_DOUBLE_EQ(half.current.depthRatio / half.fresh.depthRatio,
                     half.current.rimHeightRatio / half.fresh.rimHeightRatio);
    const CraterState basin = degradation.State(Crater{0.0, 0.0, 300000.0, 3.9, 2u});
    ASSERT_LT(basin.fresh.depthRatio, 0.04) << "fresh shallower than the cutoff";
    EXPECT_EQ(basin.current, basin.fresh) << "never deeper or higher-rimmed than when fresh";
    EXPECT_THROW(FreshnessDegradation(nullptr, LunarFreshCraterMorphometry), std::invalid_argument);
    EXPECT_THROW(FreshnessDegradation(std::make_shared<UniformAges>(1.0), nullptr),
                 std::invalid_argument);
    EXPECT_THROW(
        FreshnessDegradation(std::make_shared<UniformAges>(1.0), LunarFreshCraterMorphometry, 0.0),
        std::invalid_argument);
    EXPECT_THROW(
        FreshnessDegradation(std::make_shared<UniformAges>(1.0), LunarFreshCraterMorphometry, 0.1),
        std::invalid_argument);
}
