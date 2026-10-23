#include "terraingen/layers/craters/shaping/attributes.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using terraingen::ComputeCraterAttributes;
using terraingen::CraterState;

TEST(ComputeCraterAttributes, ReadsTheFloorAndRimFromTheProfile) {
    const terraingen::Crater crater{0.0, 0.0, 100.0, 1.0, 1u};
    const CraterState fresh{{0.2, 0.04}, {0.2, 0.04}};
    for (const auto& profile : std::initializer_list<std::shared_ptr<terraingen::CraterProfile>>{
             std::make_shared<terraingen::FassettThomsonProfile>(),
             std::make_shared<terraingen::MartinParkesDunstanProfile>()}) {
        const auto attributes = ComputeCraterAttributes(crater, fresh, *profile);
        EXPECT_EQ(attributes.floorDepth, -profile->Height(0.0, 100.0, fresh));
        EXPECT_GT(attributes.floorDepth, 10.0);
        EXPECT_GE(attributes.rimHeight, profile->Height(50.0, 100.0, fresh));
        EXPECT_NEAR(attributes.rimHeight, 4.0, 0.6);
        EXPECT_EQ(attributes.depthRatio, 0.2);
        EXPECT_EQ(attributes.freshness, 1.0);
    }
    const CraterState worn{{0.2, 0.04}, {0.05, 0.01}};
    const terraingen::FassettThomsonProfile profile;
    const auto attributes = ComputeCraterAttributes(crater, worn, profile);
    EXPECT_EQ(attributes.freshness, 0.25);
    EXPECT_EQ(attributes.depthRatio, 0.05);
    EXPECT_LT(attributes.floorDepth, ComputeCraterAttributes(crater, fresh, profile).floorDepth);
    EXPECT_EQ(ComputeCraterAttributes(crater, CraterState{}, profile).freshness, 1.0)
        << "no fresh depth to compare with";
}

TEST(ComputeCraterAttributes, FindsTheFloorOfComplexCratersBesideTheirPeaks) {
    const terraingen::ComplexCraterProfile profile(
        std::make_shared<terraingen::FassettThomsonProfile>(),
        terraingen::LunarComplexCraterShape());
    const auto morphometry = terraingen::LunarFreshCraterMorphometry(30000.0);
    const CraterState state{morphometry, morphometry};
    const auto attributes = ComputeCraterAttributes({0.0, 0.0, 30000.0, 1.0, 1u}, state, profile);
    EXPECT_TRUE(attributes.complex);
    EXPECT_NEAR(attributes.floorDepth,
                (morphometry.depthRatio - morphometry.rimHeightRatio) * 30000.0, 1e-7 * 30000.0);
    EXPECT_NEAR(attributes.rimHeight, morphometry.rimHeightRatio * 30000.0, 1e-7 * 30000.0);
    EXPECT_FALSE(ComputeCraterAttributes({0.0, 0.0, 300.0, 1.0, 1u}, state, profile).complex);
}

TEST(CraterState, FreshnessIsTheDepthNowOverTheDepthWhenFresh) {
    EXPECT_EQ((CraterState{{0.2, 0.04}, {0.05, 0.01}}.Freshness()), 0.25);
    EXPECT_EQ((CraterState{{0.2, 0.04}, {0.2, 0.04}}.Freshness()), 1.0);
    EXPECT_EQ(CraterState{}.Freshness(), 1.0) << "no depth when fresh";
}
