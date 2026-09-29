#include "terraingen/kernels/fbm.h"

#include <gtest/gtest.h>

#include <cmath>

using terraingen::Fbm2;
using terraingen::FbmParams;
using terraingen::GainFromHurst;

namespace {

double MeanNeighbourDifference(const FbmParams& params) {
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            sum +=
                static_cast<double>(std::fabs(Fbm2(fx + 1.0f, fy, params) - Fbm2(fx, fy, params)));
            ++count;
        }
    }
    return sum / count;
}

}  // namespace

TEST(Fbm2, StaysWithinAmplitude) {
    FbmParams params;
    params.amplitude = 50.0f;
    params.wavelength = 64.0f;
    float maxAbs = 0.0f;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            maxAbs = std::max(maxAbs, std::fabs(Fbm2(static_cast<float>(x) * 1.7f,
                                                     static_cast<float>(y) * 1.3f, params)));
        }
    }
    EXPECT_LE(maxAbs, params.amplitude * (1.0f + 1e-5f));
    EXPECT_GT(maxAbs, 0.2f * params.amplitude);
}

TEST(Fbm2, IsNotPinnedToZeroAtTheOrigin) {
    int nonZero = 0;
    for (std::uint32_t seed = 0; seed < 20u; ++seed) {
        FbmParams params;
        params.seed = seed;
        if (Fbm2(0.0f, 0.0f, params) != 0.0f) {
            ++nonZero;
        }
    }
    EXPECT_EQ(nonZero, 20);
}

TEST(Fbm2, IsDeterministicPerSeedAndVariesAcrossSeeds) {
    FbmParams a;
    FbmParams b;
    b.seed = 1u;
    EXPECT_EQ(Fbm2(123.5f, -77.25f, a), Fbm2(123.5f, -77.25f, a));
    EXPECT_NE(Fbm2(123.5f, -77.25f, a), Fbm2(123.5f, -77.25f, b));
}

TEST(Fbm2, HigherGainIsRougherAtShortBaselines) {
    FbmParams smooth;
    smooth.wavelength = 128.0f;
    smooth.gain = 0.3f;
    FbmParams rough = smooth;
    rough.gain = 0.7f;
    EXPECT_GT(MeanNeighbourDifference(rough), 1.5 * MeanNeighbourDifference(smooth));
}

TEST(Fbm2, ScalesLinearlyWithAmplitude) {
    FbmParams base;
    FbmParams doubled = base;
    doubled.amplitude = 2.0f * base.amplitude;
    EXPECT_FLOAT_EQ(Fbm2(10.0f, 20.0f, doubled), 2.0f * Fbm2(10.0f, 20.0f, base));
}

TEST(GainFromHurst, MatchesLacunarityPower) {
    EXPECT_FLOAT_EQ(GainFromHurst(1.0f, 2.0f), 0.5f);
    EXPECT_FLOAT_EQ(GainFromHurst(0.5f, 4.0f), 0.5f);
    EXPECT_FLOAT_EQ(GainFromHurst(0.0f, 2.0f), 1.0f);
}
