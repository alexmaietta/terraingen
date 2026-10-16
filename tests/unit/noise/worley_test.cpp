#include "terraingen/noise/worley.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::Worley2;

namespace {

constexpr std::uint32_t kSeed = 7u;

constexpr float F(int v) {
    return static_cast<float>(v);
}

float BruteForceF1(float x, float y) {
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    float best = 1e30f;
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const auto cx = static_cast<std::int32_t>(cellX) + dx;
            const auto cy = static_cast<std::int32_t>(cellY) + dy;
            const terraingen::UVec3 h = terraingen::Pcg3dHash(terraingen::UVec3{
                static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy), kSeed});
            const float px = static_cast<float>(cx) + terraingen::HashToUnitFloat(h.y);
            const float py = static_cast<float>(cy) + terraingen::HashToUnitFloat(h.z);
            best = std::fmin(best, std::hypot(px - x, py - y));
        }
    }
    return best;
}

}  // namespace

TEST(Worley2, F1MatchesABruteForceSearch) {
    for (int i = 0; i < 500; ++i) {
        const float x = F(i % 25) * 0.83f - 9.0f;
        const float y = F(i / 25) * 0.67f + 3.0f;
        EXPECT_NEAR(Worley2(x, y, kSeed).f1, BruteForceF1(x, y), 1e-5f);
    }
}

TEST(Worley2, DistancesAreOrderedAndBounded) {
    for (int i = 0; i < 2000; ++i) {
        const auto d = Worley2(F(i % 50) * 0.37f, F(i / 50) * 0.41f, kSeed);
        EXPECT_GE(d.f1, 0.0f);
        EXPECT_LE(d.f1, d.f2);
        EXPECT_LE(d.f1, std::sqrt(2.0f));
        EXPECT_LE(d.f2, std::sqrt(5.0f));
    }
}

TEST(Worley2, IsContinuous) {
    constexpr float kEps = 1e-4f;
    for (int i = 0; i < 400; ++i) {
        const float x = F(i) * 0.173f - 30.0f;
        const auto a = Worley2(x, 2.5f, kSeed);
        const auto b = Worley2(x + kEps, 2.5f, kSeed);
        EXPECT_NEAR(a.f1, b.f1, 2e-4f);
        EXPECT_NEAR(a.f2, b.f2, 2e-4f);
    }
}

TEST(Worley2, DependsOnSeed) {
    EXPECT_NE(Worley2(1.3f, 2.7f, 1u).f1, Worley2(1.3f, 2.7f, 2u).f1);
}
