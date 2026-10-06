#include "terraingen/terraingen.h"

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

TEST(NoDegradation, KeepsEveryCraterFresh) {
    const NoDegradation fresh;
    const CraterState state = fresh.State(Crater{0.0, 0.0, 150.0, 3.0, 1u});
    EXPECT_EQ(state.fresh.depthRatio, 0.15);
    EXPECT_EQ(state.fresh.rimHeightRatio, kFreshRimHeightRatio);
    EXPECT_EQ(state.current.depthRatio, state.fresh.depthRatio);
    EXPECT_EQ(state.current.rimHeightRatio, state.fresh.rimHeightRatio);
    const NoDegradation fixed(0.2);
    EXPECT_EQ(fixed.State(Crater{0.0, 0.0, 5.0, 0.0, 1u}).current.depthRatio, 0.2);
    EXPECT_THROW(NoDegradation(0.0), std::invalid_argument);
    EXPECT_THROW(NoDegradation(std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(fresh.State(Crater{0.0, 0.0, -1.0, 0.0, 1u}), std::domain_error);
    const std::unique_ptr<DegradationModel> base = std::make_unique<NoDegradation>();
    EXPECT_EQ(base->State(Crater{0.0, 0.0, 500.0, 0.0, 1u}).current.depthRatio, 0.21);
}
