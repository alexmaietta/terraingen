#include "terraingen/noise/kernels/layers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::BasisNoise3;
using terraingen::EvaluateNoiseLayers3;
using terraingen::Fractal3;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::HasBasisNoise3;
using terraingen::LayerBlend;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::PerlinNoise3;
using terraingen::ValueNoise3;

TEST(ValueNoise3, InterpolatesLatticeValuesWithinRangeAndContinuously) {
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < 4000; ++i) {
        const float fx = static_cast<float>(i % 13) / 13.0f;
        const float fy = static_cast<float>(i % 7) / 7.0f;
        const float fz = static_cast<float>(i % 11) / 11.0f;
        const float v = ValueNoise3(i % 17 - 8, i / 17 - 100, i % 5, fx, fy, fz, 3u);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
        sum += static_cast<double>(v);
        ++count;
    }
    EXPECT_NEAR(sum / count, 0.0, 0.1);
    EXPECT_NEAR(ValueNoise3(2, 3, 4, 0.999999f, 0.5f, 0.25f, 9u),
                ValueNoise3(3, 3, 4, 0.0f, 0.5f, 0.25f, 9u), 1e-4f);
    EXPECT_NEAR(ValueNoise3(2, 3, 4, 0.5f, 0.5f, 0.999999f, 9u),
                ValueNoise3(2, 3, 5, 0.5f, 0.5f, 0.0f, 9u), 1e-4f);
    EXPECT_NE(ValueNoise3(2, 3, 4, 0.0f, 0.0f, 0.0f, 9u),
              ValueNoise3(2, 3, 4, 0.0f, 0.0f, 0.0f, 10u));
}

TEST(BasisNoise3, OffersPerlinAndValueNoise) {
    EXPECT_TRUE(HasBasisNoise3(NoiseBasis::Perlin));
    EXPECT_TRUE(HasBasisNoise3(NoiseBasis::Value));
    EXPECT_FALSE(HasBasisNoise3(NoiseBasis::Simplex));
    EXPECT_FALSE(HasBasisNoise3(NoiseBasis::WorleyF1));
    EXPECT_EQ(BasisNoise3(NoiseBasis::Perlin, 1, 2, 3, 0.3f, 0.6f, 0.2f, 5u),
              PerlinNoise3(1, 2, 3, 0.3f, 0.6f, 0.2f, 5u));
    EXPECT_EQ(BasisNoise3(NoiseBasis::Value, 1, 2, 3, 0.3f, 0.6f, 0.2f, 5u),
              ValueNoise3(1, 2, 3, 0.3f, 0.6f, 0.2f, 5u));
    EXPECT_EQ(BasisNoise3(NoiseBasis::Simplex, 1, 2, 3, 0.3f, 0.6f, 0.2f, 5u),
              PerlinNoise3(1, 2, 3, 0.3f, 0.6f, 0.2f, 5u));
}

TEST(Fractal3, StaysWithinItsAmplitudeForEveryTypeAndIsPreciseFarFromTheOrigin) {
    for (const FractalType type : {FractalType::Fbm, FractalType::Ridged, FractalType::Billow,
                                   FractalType::HybridMultifractal}) {
        FractalParams params;
        params.type = type;
        params.amplitude = 7.0f;
        params.wavelength = 50.0f;
        params.octaves = 5;
        for (int i = 0; i < 500; ++i) {
            const float h = Fractal3(1737400.0 + 3.1 * i, -12.7 * i, 0.5 * i, params);
            EXPECT_LE(std::fabs(h), 7.0f + 1e-4f);
        }
    }
    FractalParams fine;
    fine.wavelength = 1.0f;
    fine.octaves = 1;
    fine.amplitude = 1.0f;
    const double far = 1737400.0;
    const float a = Fractal3(far, 0.0, 0.0, fine);
    const float b = Fractal3(far + 0.01, 0.0, 0.0, fine);
    const float c = Fractal3(far + 0.4, 0.0, 0.0, fine);
    EXPECT_NE(a, b);
    EXPECT_LT(std::fabs(a - b), 0.1f);
    EXPECT_NE(a, c);
}

TEST(EvaluateNoiseLayers3, BlendsWarpsAndSkipsDisabledLayers) {
    NoiseLayer base;
    base.fractal.amplitude = 10.0f;
    base.fractal.wavelength = 100.0f;
    NoiseLayer detail = base;
    detail.fractal.seed = 4;
    detail.fractal.amplitude = 2.0f;
    const double x = 1000.0;
    const double y = -2000.0;
    const double z = 1.7e6;
    FractalParams unit = base.fractal;
    unit.amplitude = 1.0f;
    const float first = 10.0f * Fractal3(x, y, z, unit);
    unit = detail.fractal;
    unit.amplitude = 1.0f;
    const float second = Fractal3(x, y, z, unit);
    const NoiseLayer pair[] = {base, detail};
    EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, pair, 2), first + 2.0f * second);
    for (const LayerBlend blend : {LayerBlend::Max, LayerBlend::Min, LayerBlend::Mask}) {
        NoiseLayer blended = detail;
        blended.blend = blend;
        const NoiseLayer layers[] = {base, blended};
        const float expected = blend == LayerBlend::Max   ? std::max(first, 2.0f * second)
                               : blend == LayerBlend::Min ? std::min(first, 2.0f * second)
                                                          : first * 0.5f * (second + 1.0f);
        EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, layers, 2), expected);
    }
    NoiseLayer off = base;
    off.enabled = false;
    const NoiseLayer skipped[] = {off, detail};
    EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, skipped, 2), 2.0f * second);
    EXPECT_EQ(EvaluateNoiseLayers3(x, y, z, &off, 1), 0.0f);
    NoiseLayer warped = base;
    warped.warpAmplitude = 30.0f;
    warped.warpWavelength = 400.0f;
    EXPECT_NE(EvaluateNoiseLayers3(x, y, z, &warped, 1), first);
    EXPECT_LE(std::fabs(EvaluateNoiseLayers3(x, y, z, &warped, 1)), 10.0f + 1e-4f);
}
