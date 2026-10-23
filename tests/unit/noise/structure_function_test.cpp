#include "terraingen/noise/structure_function.h"

#include "terraingen/noise/perlin.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::PerlinStructureFunction;

TEST(PerlinStructureFunction, FollowsItsLimitsAndIsContinuous) {
    EXPECT_EQ(PerlinStructureFunction(0.0), 0.0);
    EXPECT_EQ(PerlinStructureFunction(-1.0), 0.0);
    EXPECT_NEAR(PerlinStructureFunction(1e-4) / 1e-8, 0.7724, 1e-3);
    EXPECT_EQ(PerlinStructureFunction(100.0), 0.18312);
    double previous = 0.0;
    for (double r = 1e-3; r < 10.0; r *= 1.01) {
        const double value = PerlinStructureFunction(r);
        EXPECT_GE(value, previous * 0.99) << r;
        EXPECT_LT(std::fabs(PerlinStructureFunction(r * 1.0001) - value), 1e-3 * value + 1e-12)
            << r;
        previous = value;
    }
}

TEST(PerlinStructureFunction, MatchesSampledDifferencesOfThePerlinBasis) {
    for (const double r : {0.1, 0.5, 1.0, 3.0}) {
        double sum = 0.0;
        constexpr int kSamples = 1 << 16;
        for (int n = 0; n < kSamples; ++n) {
            const auto seed = static_cast<std::uint32_t>(n);
            const float x = 0.37f * static_cast<float>(n % 256) + 0.013f * static_cast<float>(n);
            const float y = 0.29f * static_cast<float>(n / 256);
            const double angle = 2.399963 * n;
            const double a = static_cast<double>(terraingen::PerlinNoise2(x, y, seed));
            const double b = static_cast<double>(terraingen::PerlinNoise2(
                static_cast<float>(static_cast<double>(x) + r * std::cos(angle)),
                static_cast<float>(static_cast<double>(y) + r * std::sin(angle)), seed));
            sum += (a - b) * (a - b);
        }
        EXPECT_NEAR(sum / kSamples, PerlinStructureFunction(r), 0.03 * PerlinStructureFunction(r))
            << r;
    }
}

TEST(Perlin3StructureFunction, RisesFromZeroToItsLargeLagValue) {
    EXPECT_EQ(terraingen::Perlin3StructureFunction(0.0), 0.0);
    EXPECT_NEAR(terraingen::Perlin3StructureFunction(1e-4) / 1e-8, 0.4941, 1e-3);
    EXPECT_EQ(terraingen::Perlin3StructureFunction(100.0), 0.145198);
}
