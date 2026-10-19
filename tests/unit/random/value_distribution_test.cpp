#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/value_distribution.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
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

TEST(ValueDistribution, PiecewiseInvertsTheBinsAndSkipsEmptyOnes) {
    ValueDistribution piecewise{ValueDistribution::Kind::Piecewise};
    piecewise.edges = {0.0, 1.0, 2.0, 4.0};
    piecewise.weights = {1.0, 0.0, 1.0};
    terraingen::ValidateValueDistribution(piecewise);
    EXPECT_DOUBLE_EQ(piecewise.Sample(0.0, 0.9), 0.0);
    EXPECT_DOUBLE_EQ(piecewise.Sample(0.25, 0.9), 0.5);
    EXPECT_DOUBLE_EQ(piecewise.Sample(0.5, 0.9), 2.0);
    EXPECT_DOUBLE_EQ(piecewise.Sample(0.75, 0.1), 3.0);
    EXPECT_EQ(piecewise.Lowest(), 0.0);
    EXPECT_EQ(piecewise.Highest(), 4.0);
    piecewise.weights = {1.0, 1.0, 0.0};
    EXPECT_LE(piecewise.Sample(std::nextafter(1.0, 0.0), 0.0), 2.0);
    piecewise.max = 1.5;
    EXPECT_EQ(piecewise.Sample(0.99, 0.0), 1.5);
    EXPECT_EQ(piecewise.Highest(), 1.5);
}

TEST(ValueDistribution, RejectsMalformedPiecewiseBins) {
    const auto piecewise = [](std::vector<double> edges, std::vector<double> weights) {
        ValueDistribution d{ValueDistribution::Kind::Piecewise};
        d.edges = std::move(edges);
        d.weights = std::move(weights);
        return d;
    };
    const std::vector<ValueDistribution> invalid{
        piecewise({}, {}),
        piecewise({0.0}, {}),
        piecewise({0.0, 1.0}, {1.0, 1.0}),
        piecewise({0.0, 1.0, 1.0}, {1.0, 1.0}),
        piecewise({1.0, 0.0}, {1.0}),
        piecewise({0.0, std::nan("")}, {1.0}),
        piecewise({0.0, 1.0}, {-1.0}),
        piecewise({0.0, 1.0}, {0.0}),
        piecewise({0.0, 1.0}, {std::numeric_limits<double>::infinity()})};
    for (const auto& distribution : invalid) {
        EXPECT_THROW(terraingen::ValidateValueDistribution(distribution), std::invalid_argument);
    }
    ValueDistribution uniform{ValueDistribution::Kind::Uniform, 0.0, 1.0};
    uniform.edges = {0.0, 1.0};
    uniform.weights = {1.0};
    EXPECT_THROW(terraingen::ValidateValueDistribution(uniform), std::invalid_argument);
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
