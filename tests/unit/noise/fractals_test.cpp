// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/fractals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using terraingen::DomainWarp2;
using terraingen::EvaluateFractal;
using terraingen::EvaluateFractals;
using terraingen::Fractal;
using terraingen::Fractal2;
using terraingen::FractalBlend;

namespace {

Fractal Layer(std::uint32_t seed, float amplitude, FractalBlend blend = FractalBlend::Add) {
    Fractal layer;
    layer.seed = seed;
    layer.amplitude = amplitude;
    layer.wavelength = 80.0f;
    layer.blend = blend;
    return layer;
}

constexpr float kX = 13.5f;
constexpr float kY = -41.25f;

}  // namespace

TEST(EvaluateFractal, IsTheUnitAmplitudeFractalWithoutWarp) {
    Fractal layer = Layer(3u, 40.0f);
    EXPECT_FLOAT_EQ(40.0f * EvaluateFractal(kX, kY, layer),
                    Fractal2(kX, kY, terraingen::FractalParamsFrom(layer)));
}

TEST(EvaluateFractal, WarpShiftsTheSamplePointByTheWarpField) {
    Fractal layer = Layer(3u, 1.0f);
    layer.warpAmplitude = 30.0f;
    layer.warpWavelength = 200.0f;
    const auto [dx, dy] = DomainWarp2(kX, kY, layer);
    EXPECT_LE(std::fabs(dx), 30.0f);
    EXPECT_LE(std::fabs(dy), 30.0f);
    EXPECT_NE(dx, dy);
    EXPECT_FLOAT_EQ(EvaluateFractal(kX, kY, layer),
                    Fractal2(kX + dx, kY + dy, terraingen::FractalParamsFrom(layer)));
}

TEST(EvaluateFractals, FirstEnabledLayerSetsTheHeight) {
    std::array<Fractal, 2> layers{Layer(1u, 10.0f), Layer(2u, 20.0f, FractalBlend::Max)};
    layers[0].enabled = false;
    EXPECT_FLOAT_EQ(EvaluateFractals(kX, kY, layers.data(), layers.size()),
                    20.0f * EvaluateFractal(kX, kY, layers[1]));
    layers[1].enabled = false;
    EXPECT_EQ(EvaluateFractals(kX, kY, layers.data(), layers.size()), 0.0f);
}

TEST(EvaluateFractals, CombinesLaterLayersByBlendMode) {
    const Fractal base = Layer(1u, 10.0f);
    const float a = 10.0f * EvaluateFractal(kX, kY, base);
    for (const FractalBlend blend :
         {FractalBlend::Add, FractalBlend::Max, FractalBlend::Min, FractalBlend::Mask}) {
        const Fractal top = Layer(2u, 4.0f, blend);
        const float value = EvaluateFractal(kX, kY, top);
        const std::array<Fractal, 2> layers{base, top};
        const float height = EvaluateFractals(kX, kY, layers.data(), layers.size());
        switch (blend) {
        case FractalBlend::Add:
            EXPECT_FLOAT_EQ(height, a + 4.0f * value);
            break;
        case FractalBlend::Max:
            EXPECT_FLOAT_EQ(height, std::fmax(a, 4.0f * value));
            break;
        case FractalBlend::Min:
            EXPECT_FLOAT_EQ(height, std::fmin(a, 4.0f * value));
            break;
        case FractalBlend::Mask:
            EXPECT_FLOAT_EQ(height, a * 0.5f * (value + 1.0f));
            break;
        }
    }
}

TEST(EvaluateFractals, FixedSizeBlockMatchesTheLayerArray) {
    std::array<Fractal, 3> layers{Layer(1u, 10.0f), Layer(2u, 4.0f, FractalBlend::Max),
                                  Layer(3u, 2.0f, FractalBlend::Mask)};
    layers[1].enabled = false;
    layers[2].warpAmplitude = 5.0f;
    terraingen::FractalStack block;
    for (const Fractal& layer : layers) {
        block.fractals[block.count++] = layer;
    }
    for (int i = 0; i < 20; ++i) {
        const float x = kX + 7.3f * static_cast<float>(i);
        EXPECT_EQ(EvaluateFractals(x, kY, block),
                  EvaluateFractals(x, kY, layers.data(), layers.size()));
    }
    block.count = 0;
    EXPECT_EQ(EvaluateFractals(kX, kY, block), 0.0f);
}

TEST(Fractal, ComparesEveryField) {
    const Fractal base;
    EXPECT_EQ(base, Fractal{});
    Fractal changed = base;
    changed.enabled = false;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.blend = FractalBlend::Min;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.warpAmplitude = 1.0f;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.warpWavelength = 1.0f;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.seed = 9u;
    EXPECT_FALSE(changed == base);
}

TEST(Fractal, FbmSetsTheSeedWavelengthAndAmplitude) {
    const terraingen::Fractal layer = terraingen::Fractal::Fbm(3, 800.0, 40.0);
    terraingen::Fractal expected;
    expected.seed = 3;
    expected.wavelength = 800.0f;
    expected.amplitude = 40.0f;
    EXPECT_EQ(layer, expected);
}

TEST(Fractal, ComparesEveryFractalParamsField) {
    const Fractal base;
    Fractal changed = base;
    changed.octaves = 3;
    EXPECT_NE(changed, base);
    changed = base;
    changed.lacunarity = 3.0f;
    EXPECT_NE(changed, base);
    changed = base;
    changed.gain = 0.7f;
    EXPECT_NE(changed, base);
    changed = base;
    changed.basis = terraingen::NoiseBasis::Simplex;
    EXPECT_NE(changed, base);
    changed = base;
    changed.type = terraingen::FractalType::Ridged;
    EXPECT_NE(changed, base);
    changed = base;
    changed.offset = 0.5f;
    EXPECT_NE(changed, base);
    changed = base;
    changed.wavelength = 1.0f;
    EXPECT_NE(changed, base);
    changed = base;
    changed.amplitude = 1.0f;
    EXPECT_NE(changed, base);
}

TEST(FractalParamsFrom, CopiesTheFractalFieldsAndKeepsTheDefaults) {
    EXPECT_EQ(terraingen::FractalParamsFrom(Fractal{}), terraingen::FractalParams{});
    Fractal fractal;
    fractal.seed = 4u;
    fractal.wavelength = 250.0f;
    fractal.amplitude = 7.0f;
    fractal.octaves = 5;
    fractal.lacunarity = 2.5f;
    fractal.gain = 0.4f;
    fractal.basis = terraingen::NoiseBasis::WorleyF1;
    fractal.type = terraingen::FractalType::Billow;
    fractal.offset = 0.9f;
    fractal.blend = FractalBlend::Max;
    fractal.warpAmplitude = 3.0f;
    const terraingen::FractalParams params = terraingen::FractalParamsFrom(fractal);
    const terraingen::FractalParams expected{4u,
                                             250.0f,
                                             7.0f,
                                             5,
                                             2.5f,
                                             0.4f,
                                             terraingen::NoiseBasis::WorleyF1,
                                             terraingen::FractalType::Billow,
                                             0.9f};
    EXPECT_EQ(params, expected);
}
