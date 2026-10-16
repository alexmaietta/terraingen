#include "terraingen/noise/fractal.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <utility>

using terraingen::BasisNoise2;
using terraingen::Fractal2;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::GainFromHurst;
using terraingen::kFractalTypeCount;
using terraingen::kNoiseBasisCount;
using terraingen::NoiseBasis;
using terraingen::PerlinNoise2;
using terraingen::SimplexNoise2;
using terraingen::ValueNoise2;
using terraingen::Worley2;

namespace {

constexpr float F(int v) {
    return static_cast<float>(v);
}

double MeanNeighbourDifference(const FractalParams& params) {
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            sum += static_cast<double>(
                std::fabs(Fractal2(F(x) + 1.0f, F(y), params) - Fractal2(F(x), F(y), params)));
            ++count;
        }
    }
    return sum / count;
}

}  // namespace

TEST(BasisNoise2, DispatchesToEachKernel) {
    const float x = 3.7f;
    const float y = -1.2f;
    EXPECT_EQ(BasisNoise2(NoiseBasis::Perlin, x, y, 5u), PerlinNoise2(x, y, 5u));
    EXPECT_EQ(BasisNoise2(NoiseBasis::Simplex, x, y, 5u), SimplexNoise2(x, y, 5u));
    EXPECT_EQ(BasisNoise2(NoiseBasis::Value, x, y, 5u), ValueNoise2(x, y, 5u));
    const auto d = Worley2(x, y, 5u);
    EXPECT_FLOAT_EQ(BasisNoise2(NoiseBasis::WorleyF1, x, y, 5u),
                    2.0f * d.f1 / std::sqrt(2.0f) - 1.0f);
    EXPECT_FLOAT_EQ(BasisNoise2(NoiseBasis::WorleyF2, x, y, 5u),
                    2.0f * d.f2 / std::sqrt(5.0f) - 1.0f);
    EXPECT_FLOAT_EQ(BasisNoise2(NoiseBasis::WorleyF2MinusF1, x, y, 5u),
                    2.0f * (d.f2 - d.f1) / std::sqrt(5.0f) - 1.0f);
}

TEST(BasisNoise2, EveryBasisStaysWithinUnitRange) {
    for (std::uint32_t b = 0; b < kNoiseBasisCount; ++b) {
        for (int i = 0; i < 4000; ++i) {
            const float n =
                BasisNoise2(static_cast<NoiseBasis>(b), F(i % 80) * 0.29f, F(i / 80) * 0.31f, 11u);
            ASSERT_GE(n, -1.0f) << "basis " << b;
            ASSERT_LE(n, 1.0f) << "basis " << b;
        }
    }
}

TEST(Fractal2, EveryBasisAndTypeStaysWithinAmplitude) {
    for (std::uint32_t b = 0; b < kNoiseBasisCount; ++b) {
        for (std::uint32_t t = 0; t < kFractalTypeCount; ++t) {
            for (const float offset : {0.3f, 1.0f, 1.7f}) {
                FractalParams params;
                params.basis = static_cast<NoiseBasis>(b);
                params.type = static_cast<FractalType>(t);
                params.amplitude = 25.0f;
                params.wavelength = 40.0f;
                params.octaves = 6;
                params.gain = 0.7f;
                params.offset = offset;
                float maxAbs = 0.0f;
                for (int i = 0; i < 1600; ++i) {
                    maxAbs = std::fmax(
                        maxAbs, std::fabs(Fractal2(F(i % 40) * 3.1f, F(i / 40) * 2.9f, params)));
                }
                EXPECT_LE(maxAbs, params.amplitude * (1.0f + 1e-5f))
                    << "basis " << b << " type " << t << " offset " << offset;
                EXPECT_GT(maxAbs, 0.05f * params.amplitude)
                    << "basis " << b << " type " << t << " offset " << offset;
            }
        }
    }
}

TEST(Fractal2, IsNotPinnedToZeroAtTheOrigin) {
    int nonZero = 0;
    for (std::uint32_t seed = 0; seed < 20u; ++seed) {
        FractalParams params;
        params.seed = seed;
        nonZero += Fractal2(0.0f, 0.0f, params) != 0.0f ? 1 : 0;
    }
    EXPECT_EQ(nonZero, 20);
}

TEST(Fractal2, IsDeterministicPerSeedAndVariesAcrossSeeds) {
    FractalParams a;
    FractalParams b;
    b.seed = 1u;
    EXPECT_EQ(Fractal2(123.5f, -77.25f, a), Fractal2(123.5f, -77.25f, a));
    EXPECT_NE(Fractal2(123.5f, -77.25f, a), Fractal2(123.5f, -77.25f, b));
}

TEST(Fractal2, HigherGainIsRougherAtShortBaselines) {
    FractalParams smooth;
    smooth.wavelength = 128.0f;
    smooth.gain = 0.3f;
    FractalParams rough = smooth;
    rough.gain = 0.7f;
    EXPECT_GT(MeanNeighbourDifference(rough), 1.5 * MeanNeighbourDifference(smooth));
}

TEST(Fractal2, ScalesLinearlyWithAmplitude) {
    for (std::uint32_t t = 0; t < kFractalTypeCount; ++t) {
        FractalParams base;
        base.type = static_cast<FractalType>(t);
        FractalParams doubled = base;
        doubled.amplitude = 2.0f * base.amplitude;
        EXPECT_FLOAT_EQ(Fractal2(10.0f, 20.0f, doubled), 2.0f * Fractal2(10.0f, 20.0f, base));
    }
}

TEST(Fractal2, BillowIsTheFbmOfAbsoluteNoise) {
    FractalParams params;
    params.type = FractalType::Billow;
    params.octaves = 1;
    params.amplitude = 1.0f;
    const terraingen::UVec3 h = terraingen::Pcg3dHash(terraingen::UVec3{0u, 0u, 0u});
    const float n = PerlinNoise2(5.0f / params.wavelength + terraingen::HashToUnitFloat(h.y),
                                 7.0f / params.wavelength + terraingen::HashToUnitFloat(h.z), h.x);
    EXPECT_FLOAT_EQ(Fractal2(5.0f, 7.0f, params), 2.0f * std::fabs(n) - 1.0f);
}

TEST(Fractal2, RidgedPeaksWhereTheBasisCrossesZero) {
    FractalParams params;
    params.type = FractalType::Ridged;
    params.octaves = 1;
    params.amplitude = 1.0f;
    const terraingen::UVec3 h = terraingen::Pcg3dHash(terraingen::UVec3{0u, 0u, 0u});
    const float x = (3.0f - terraingen::HashToUnitFloat(h.y)) * params.wavelength;
    const float y = (-2.0f - terraingen::HashToUnitFloat(h.z)) * params.wavelength;
    EXPECT_NEAR(Fractal2(x, y, params), 1.0f, 1e-4f);
}

TEST(Fractal2, TurnsEachOctaveByTheGoldenAngle) {
    FractalParams params;
    params.seed = 9u;
    params.octaves = 2;
    params.wavelength = 50.0f;
    params.amplitude = 1.0f;
    const double turn = 3.14159265358979 * (3.0 - std::sqrt(5.0));
    const float c = static_cast<float>(std::cos(turn));
    const float s = static_cast<float>(std::sin(turn));
    for (const auto& [x, y] : {std::pair{13.0f, -40.0f}, std::pair{-210.5f, 77.25f}}) {
        const auto h0 = terraingen::Pcg3dHash({params.seed, 0u, 0u});
        const auto h1 = terraingen::Pcg3dHash({params.seed, 1u, 0u});
        const float first = PerlinNoise2(x / 50.0f + terraingen::HashToUnitFloat(h0.y),
                                         y / 50.0f + terraingen::HashToUnitFloat(h0.z), h0.x);
        const float second =
            PerlinNoise2((x * c - y * s) / 25.0f + terraingen::HashToUnitFloat(h1.y),
                         (x * s + y * c) / 25.0f + terraingen::HashToUnitFloat(h1.z), h1.x);
        EXPECT_NEAR(Fractal2(x, y, params), (first + 0.5f * second) / 1.5f, 1e-5f);
    }
}

TEST(FractalParams, ComparesEveryField) {
    const FractalParams base;
    EXPECT_EQ(base, FractalParams{});
    const auto differs = [&base](auto mutate) {
        FractalParams changed = base;
        mutate(changed);
        return !(changed == base);
    };
    EXPECT_TRUE(differs([](FractalParams& p) { p.seed = 1u; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.wavelength = 1.0f; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.amplitude = 1.0f; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.octaves = 1; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.lacunarity = 3.0f; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.gain = 0.25f; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.basis = NoiseBasis::Value; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.type = FractalType::Ridged; }));
    EXPECT_TRUE(differs([](FractalParams& p) { p.offset = 0.5f; }));
}

TEST(GainFromHurst, MatchesLacunarityPower) {
    EXPECT_FLOAT_EQ(GainFromHurst(1.0f, 2.0f), 0.5f);
    EXPECT_FLOAT_EQ(GainFromHurst(0.5f, 4.0f), 0.5f);
    EXPECT_FLOAT_EQ(GainFromHurst(0.0f, 2.0f), 1.0f);
}
