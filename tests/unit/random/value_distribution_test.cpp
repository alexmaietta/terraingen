#include "terraingen/random/stream.h"
#include "terraingen/random/value_distribution.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

using terraingen::ValueDistribution;

TEST(ValueDistribution, SamplesEachKindWithinItsBounds) {
    const ValueDistribution constant{ValueDistribution::Kind::Constant, 2.0};
    EXPECT_EQ(constant.Sample(0.3, 0.7), 2.0);
    EXPECT_EQ(constant.Lowest(), 2.0);
    EXPECT_EQ(constant.Highest(), 2.0);
    const ValueDistribution uniform{ValueDistribution::Kind::Uniform, 1.0, 3.0};
    EXPECT_EQ(uniform.Sample(0.25, 0.0), 1.5);
    EXPECT_EQ(uniform.Highest(), 3.0);
    const ValueDistribution normal{ValueDistribution::Kind::Normal, 5.0, 1.0, 4.0, 6.0};
    const ValueDistribution logNormal{ValueDistribution::Kind::LogNormal, 2.0, 0.5, 0.0, 10.0};
    double mean = 0.0;
    std::vector<double> logs;
    for (int n = 1; n < 2000; ++n) {
        const double u1 = n / 2000.0;
        const double u2 = std::fmod(n * 0.618034, 1.0);
        const double value = normal.Sample(u1, u2);
        EXPECT_GE(value, 4.0);
        EXPECT_LE(value, 6.0);
        mean += value;
        logs.push_back(std::log(logNormal.Sample(u1, u2)));
    }
    EXPECT_NEAR(mean / 1999.0, 5.0, 0.05);
    std::sort(logs.begin(), logs.end());
    EXPECT_NEAR(std::exp(logs[logs.size() / 2]), 2.0, 0.1) << "the median";
    EXPECT_EQ(normal.Lowest(), 4.0);
    EXPECT_EQ(logNormal.Lowest(), 0.0);
    EXPECT_EQ(logNormal.Highest(), 10.0);
    const std::vector<ValueDistribution> invalid{
        {ValueDistribution::Kind::Uniform, 3.0, 1.0},
        {ValueDistribution::Kind::Normal, 1.0, 1.0},
        {ValueDistribution::Kind::Normal, 1.0, -1.0, 0.0, 2.0},
        {ValueDistribution::Kind::LogNormal, 0.0, 1.0, 0.0, 2.0},
        {ValueDistribution::Kind::Constant, std::nan("")},
        {ValueDistribution::Kind::Constant, 1.0, 0.0, 2.0, 1.0}};
    for (const auto& distribution : invalid) {
        EXPECT_THROW(terraingen::ValidateValueDistribution(distribution), std::invalid_argument);
    }
}

TEST(StandardNormal, IsTheBoxMullerTransform) {
    EXPECT_EQ(terraingen::StandardNormal(0.0, 0.3), 0.0);
    EXPECT_NEAR(terraingen::StandardNormal(1.0 - std::exp(-0.5), 0.0), 1.0, 1e-12);
    EXPECT_NEAR(terraingen::StandardNormal(1.0 - std::exp(-2.0), 0.5), -2.0, 1e-12);
}

TEST(SplitId, GivesTheLowAndHighWords) {
    constexpr terraingen::IdWords words = terraingen::SplitId(0x0123456789abcdefull);
    EXPECT_EQ(words.low, 0x89abcdefu);
    EXPECT_EQ(words.high, 0x01234567u);
}
