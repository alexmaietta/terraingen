#include "ernie/render/sampling.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>

using ernie::FrameSeed;
using ernie::Pcg3d;
using ernie::StreamOf;

static_assert(StreamOf("") == 2166136261u, "FNV-1a offset basis");
static_assert(StreamOf("a") == 0xe40c292cu, "FNV-1a of a");
static_assert(FrameSeed(1, 2, 3) == FrameSeed(1, 2, 3), "usable at compile time");

TEST(SamplingTest, Pcg3dMixesEveryInputWord) {
    const glm::uvec3 base = Pcg3d(glm::uvec3(0u));
    for (int word = 0; word < 3; ++word) {
        glm::uvec3 input(0u);
        input[word] = 1u;
        const glm::uvec3 h = Pcg3d(input);
        for (int out = 0; out < 3; ++out) {
            EXPECT_NE(h[out], base[out]) << "input " << word << " output " << out;
        }
    }
}

TEST(SamplingTest, StreamsOfProductPathsDiffer) {
    EXPECT_NE(StreamOf("/Render/Left"), StreamOf("/Render/Right"));
    EXPECT_EQ(StreamOf("/Render/Left"), StreamOf("/Render/Left"));
}

TEST(SamplingTest, FrameSeedsAreDistinctAcrossSeedsStreamsAndFrames) {
    std::set<std::uint32_t> seeds;
    int count = 0;
    for (std::uint32_t seed = 0; seed < 8; ++seed) {
        for (std::uint32_t stream = 0; stream < 8; ++stream) {
            for (std::uint64_t frame = 0; frame < 64; ++frame) {
                seeds.insert(FrameSeed(seed, stream, frame));
                ++count;
            }
        }
    }
    EXPECT_EQ(seeds.size(), static_cast<std::size_t>(count));
}

TEST(SamplingTest, FrameSeedUsesTheHighWordOfTheFrame) {
    EXPECT_NE(FrameSeed(0, 0, 1), FrameSeed(0, 0, (std::uint64_t{1} << 32u) + 1u));
}
