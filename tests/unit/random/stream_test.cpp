#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <array>
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

TEST(RandomStream, JoinsWhatSplitIdSplits) {
    constexpr std::uint64_t kId = 0x0123456789ABCDEFull;
    constexpr terraingen::IdWords kWords = terraingen::SplitId(kId);
    static_assert(terraingen::JoinWords(kWords.high, kWords.low) == kId);
}

TEST(RandomStream, SubstreamSeedsDifferByTagAndWordAndStayApartFromStreams) {
    using terraingen::SubstreamSeed;
    constexpr std::uint32_t kSeed = SubstreamSeed(7u, terraingen::kDuneDefectStream);
    EXPECT_EQ(SubstreamSeed(7u, terraingen::kDuneDefectStream), kSeed);
    EXPECT_NE(SubstreamSeed(7u, terraingen::kRippleKinkStream), kSeed);
    EXPECT_NE(SubstreamSeed(8u, terraingen::kDuneDefectStream), kSeed);
    EXPECT_NE(SubstreamSeed(7u, terraingen::kDuneDefectStream, 1u), kSeed);
    EXPECT_NE(RandomStream(7u, terraingen::kDuneDefectStream).NextU32(), kSeed);
}

TEST(RandomStream, StreamTagsAreDistinct) {
    EXPECT_TRUE(terraingen::detail::AllDistinct(terraingen::detail::kStreamTags));
    EXPECT_FALSE(terraingen::detail::AllDistinct(std::array<std::uint32_t, 3>{1u, 2u, 1u}));
}

TEST(StandardNormal, DrawsTwoDoublesFromTheStream) {
    RandomStream stream(5u, 6u);
    RandomStream copy(5u, 6u);
    const double u1 = copy.NextDouble();
    const double u2 = copy.NextDouble();
    EXPECT_EQ(terraingen::StandardNormal(stream), terraingen::StandardNormal(u1, u2));
    EXPECT_EQ(stream.NextU32(), copy.NextU32());
}
