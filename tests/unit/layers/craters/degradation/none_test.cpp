#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterState;
using terraingen::DegradationModel;
using terraingen::kLunarFreshRimHeightRatio;
using terraingen::LunarFreshCraterMorphometry;
using terraingen::NoDegradation;

TEST(NoDegradation, KeepsEveryCraterFresh) {
    const NoDegradation fresh(LunarFreshCraterMorphometry);
    const CraterState state = fresh.State(Crater{0.0, 0.0, 150.0, 3.0, 1u});
    EXPECT_EQ(state.fresh.depthRatio, 0.15);
    EXPECT_EQ(state.fresh.rimHeightRatio, kLunarFreshRimHeightRatio);
    EXPECT_EQ(state.current.depthRatio, state.fresh.depthRatio);
    EXPECT_EQ(state.current.rimHeightRatio, state.fresh.rimHeightRatio);
    const NoDegradation fixed(LunarFreshCraterMorphometry, 0.2);
    EXPECT_EQ(fixed.State(Crater{0.0, 0.0, 5.0, 0.0, 1u}).current.depthRatio, 0.2);
    EXPECT_EQ(fixed.State(Crater{0.0, 0.0, 5.0, 0.0, 1u}).current.rimHeightRatio,
              kLunarFreshRimHeightRatio);
    EXPECT_THROW(NoDegradation(LunarFreshCraterMorphometry, 0.0), std::invalid_argument);
    EXPECT_THROW(
        NoDegradation(LunarFreshCraterMorphometry, std::numeric_limits<double>::infinity()),
        std::invalid_argument);
    EXPECT_THROW(NoDegradation(nullptr), std::invalid_argument);
    EXPECT_THROW(fresh.State(Crater{0.0, 0.0, -1.0, 0.0, 1u}), std::domain_error);
    const std::unique_ptr<DegradationModel> base = std::make_unique<NoDegradation>(
        [](double) { return terraingen::CraterMorphometry{0.2, 0.05}; });
    EXPECT_EQ(base->State(Crater{0.0, 0.0, 500.0, 0.0, 1u}).current.depthRatio, 0.2);
    EXPECT_EQ(base->State(Crater{0.0, 0.0, 500.0, 0.0, 1u}).fresh.rimHeightRatio, 0.05);
}
