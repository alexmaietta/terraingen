#include "terraingen/random/random_stream.h"
#include "terraingen/random/samplers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::Philox4x32;
using terraingen::RandomStream;
using terraingen::SampleExponential;
using terraingen::SampleNormal;
using terraingen::SamplePoisson;
using terraingen::SampleTruncatedPowerLaw;
using terraingen::TabulatedDistribution;
using terraingen::U32x2;
using terraingen::U32x4;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::vector<std::uint32_t> Draw(RandomStream stream, int count) {
    std::vector<std::uint32_t> words;
    for (int i = 0; i < count; ++i) {
        words.push_back(stream.NextU32());
    }
    return words;
}

}  // namespace

TEST(RandomStream, ReadsConsecutivePhiloxBlocks) {
    RandomStream stream(11u, 22u, 1u, 2u, 3u);
    const U32x4 first = Philox4x32(U32x4{1u, 2u, 3u, 0u}, U32x2{11u, 22u});
    const U32x4 second = Philox4x32(U32x4{1u, 2u, 3u, 1u}, U32x2{11u, 22u});
    EXPECT_EQ(Draw(stream, 6),
              (std::vector<std::uint32_t>{first.x, first.y, first.z, first.w, second.x, second.y}));
}

TEST(RandomStream, IsDeterministicAndSeparatesEveryAddressWord) {
    const auto base = Draw(RandomStream(1u, 2u, 3u, 4u, 5u), 8);
    EXPECT_EQ(Draw(RandomStream(1u, 2u, 3u, 4u, 5u), 8), base);
    EXPECT_NE(Draw(RandomStream(0u, 2u, 3u, 4u, 5u), 8), base);
    EXPECT_NE(Draw(RandomStream(1u, 0u, 3u, 4u, 5u), 8), base);
    EXPECT_NE(Draw(RandomStream(1u, 2u, 0u, 4u, 5u), 8), base);
    EXPECT_NE(Draw(RandomStream(1u, 2u, 3u, 0u, 5u), 8), base);
    EXPECT_NE(Draw(RandomStream(1u, 2u, 3u, 4u, 0u), 8), base);
}

TEST(RandomStream, UniformsUseTheExpectedBits) {
    RandomStream words(5u, 6u);
    const std::uint32_t a = words.NextU32();
    const std::uint32_t b = words.NextU32();
    const std::uint32_t c = words.NextU32();

    RandomStream uniforms(5u, 6u);
    EXPECT_EQ(uniforms.NextFloat(), static_cast<float>(a >> 8u) * 0x1p-24f);
    const double expected =
        static_cast<double>((static_cast<std::uint64_t>(b >> 5u) << 26u) | (c >> 6u)) * 0x1p-53;
    EXPECT_EQ(uniforms.NextDouble(), expected);
}

TEST(RandomStream, UniformRangesAreRespected) {
    RandomStream stream(9u, 9u);
    for (int i = 0; i < 10000; ++i) {
        const float f = stream.NextFloat();
        EXPECT_GE(f, 0.0f);
        EXPECT_LT(f, 1.0f);
        const double d = stream.NextDouble();
        EXPECT_GE(d, 0.0);
        EXPECT_LT(d, 1.0);
        const double open = stream.NextDoubleOpenBelow();
        EXPECT_GT(open, 0.0);
        EXPECT_LE(open, 1.0);
    }
}

TEST(SamplePoisson, HandlesZeroAndRejectsInvalidMeans) {
    RandomStream stream(1u, 1u);
    EXPECT_EQ(SamplePoisson(stream, 0.0), 0u);
    EXPECT_THROW(SamplePoisson(stream, -1.0), std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, kNaN), std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, kInf), std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, 2e9), std::invalid_argument);
}

TEST(SampleTruncatedPowerLaw, StaysWithinBoundsAndRejectsInvalidArguments) {
    RandomStream stream(2u, 2u);
    for (int i = 0; i < 1000; ++i) {
        const double x = SampleTruncatedPowerLaw(stream, 2.0, 50.0, 2.5);
        EXPECT_GE(x, 2.0);
        EXPECT_LE(x, 50.0);
        EXPECT_GE(SampleTruncatedPowerLaw(stream, 1.0, kInf, 1.5), 1.0);
    }
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, 0.0, 10.0, 2.0), std::invalid_argument);
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, 5.0, 5.0, 2.0), std::invalid_argument);
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, kInf, kInf, 2.0), std::invalid_argument);
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, 1.0, kNaN, 2.0), std::invalid_argument);
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, 1.0, 10.0, 0.0), std::invalid_argument);
    EXPECT_THROW(SampleTruncatedPowerLaw(stream, 1.0, 10.0, kInf), std::invalid_argument);
}

TEST(SampleExponentialAndNormal, RejectInvalidArguments) {
    RandomStream stream(3u, 3u);
    EXPECT_THROW(SampleExponential(stream, 0.0), std::invalid_argument);
    EXPECT_THROW(SampleExponential(stream, kInf), std::invalid_argument);
    EXPECT_THROW(SampleNormal(stream, kNaN, 1.0), std::invalid_argument);
    EXPECT_THROW(SampleNormal(stream, 0.0, -1.0), std::invalid_argument);
    EXPECT_THROW(SampleNormal(stream, 0.0, kInf), std::invalid_argument);
    EXPECT_EQ(SampleNormal(stream, 3.5, 0.0), 3.5);
    EXPECT_GT(SampleExponential(stream, 2.0), 0.0);
}

TEST(TabulatedDistribution, InterpolatesItsCdfAndClampsOutside) {
    const TabulatedDistribution table({1.0, 2.0, 4.0}, {0.0, 0.5, 1.0});
    EXPECT_EQ(table.Cdf(0.5), 0.0);
    EXPECT_EQ(table.Cdf(1.0), 0.0);
    EXPECT_DOUBLE_EQ(table.Cdf(1.5), 0.25);
    EXPECT_DOUBLE_EQ(table.Cdf(3.0), 0.75);
    EXPECT_EQ(table.Cdf(4.0), 1.0);
    EXPECT_EQ(table.Cdf(9.0), 1.0);
}

TEST(TabulatedDistribution, SamplesStayInRangeAndSkipFlatSegments) {
    const TabulatedDistribution table({0.0, 1.0, 2.0, 3.0}, {0.0, 0.5, 0.5, 1.0});
    RandomStream stream(4u, 4u);
    for (int i = 0; i < 5000; ++i) {
        const double x = table.Sample(stream);
        EXPECT_GE(x, 0.0);
        EXPECT_LE(x, 3.0);
        EXPECT_FALSE(x > 1.0 && x < 2.0) << x;
    }
}

TEST(TabulatedDistribution, RejectsMalformedTables) {
    EXPECT_THROW(TabulatedDistribution({1.0}, {0.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, 2.0}, {0.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, kInf}, {0.0, 1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, 2.0}, {0.0, kNaN}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({2.0, 1.0}, {0.0, 1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, 2.0, 3.0}, {0.0, 0.6, 0.4}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, 2.0}, {0.1, 1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedDistribution({1.0, 2.0}, {0.0, 0.9}), std::invalid_argument);
}
