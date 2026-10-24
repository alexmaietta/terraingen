// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/noise_layers.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using terraingen::DomainWarp2;
using terraingen::EvaluateNoiseLayer;
using terraingen::EvaluateNoiseLayers;
using terraingen::Fractal2;
using terraingen::LayerBlend;
using terraingen::NoiseLayer;

namespace {

NoiseLayer Layer(std::uint32_t seed, float amplitude, LayerBlend blend = LayerBlend::Add) {
    NoiseLayer layer;
    layer.fractal.seed = seed;
    layer.fractal.amplitude = amplitude;
    layer.fractal.wavelength = 80.0f;
    layer.blend = blend;
    return layer;
}

constexpr float kX = 13.5f;
constexpr float kY = -41.25f;

}  // namespace

TEST(EvaluateNoiseLayer, IsTheUnitAmplitudeFractalWithoutWarp) {
    NoiseLayer layer = Layer(3u, 40.0f);
    EXPECT_FLOAT_EQ(40.0f * EvaluateNoiseLayer(kX, kY, layer), Fractal2(kX, kY, layer.fractal));
}

TEST(EvaluateNoiseLayer, WarpShiftsTheSamplePointByTheWarpField) {
    NoiseLayer layer = Layer(3u, 1.0f);
    layer.warpAmplitude = 30.0f;
    layer.warpWavelength = 200.0f;
    const auto [dx, dy] = DomainWarp2(kX, kY, layer);
    EXPECT_LE(std::fabs(dx), 30.0f);
    EXPECT_LE(std::fabs(dy), 30.0f);
    EXPECT_NE(dx, dy);
    EXPECT_FLOAT_EQ(EvaluateNoiseLayer(kX, kY, layer), Fractal2(kX + dx, kY + dy, layer.fractal));
}

TEST(EvaluateNoiseLayers, FirstEnabledLayerSetsTheHeight) {
    std::array<NoiseLayer, 2> layers{Layer(1u, 10.0f), Layer(2u, 20.0f, LayerBlend::Max)};
    layers[0].enabled = false;
    EXPECT_FLOAT_EQ(EvaluateNoiseLayers(kX, kY, layers.data(), layers.size()),
                    20.0f * EvaluateNoiseLayer(kX, kY, layers[1]));
    layers[1].enabled = false;
    EXPECT_EQ(EvaluateNoiseLayers(kX, kY, layers.data(), layers.size()), 0.0f);
}

TEST(EvaluateNoiseLayers, CombinesLaterLayersByBlendMode) {
    const NoiseLayer base = Layer(1u, 10.0f);
    const float a = 10.0f * EvaluateNoiseLayer(kX, kY, base);
    for (const LayerBlend blend :
         {LayerBlend::Add, LayerBlend::Max, LayerBlend::Min, LayerBlend::Mask}) {
        const NoiseLayer top = Layer(2u, 4.0f, blend);
        const float value = EvaluateNoiseLayer(kX, kY, top);
        const std::array<NoiseLayer, 2> layers{base, top};
        const float height = EvaluateNoiseLayers(kX, kY, layers.data(), layers.size());
        switch (blend) {
        case LayerBlend::Add:
            EXPECT_FLOAT_EQ(height, a + 4.0f * value);
            break;
        case LayerBlend::Max:
            EXPECT_FLOAT_EQ(height, std::fmax(a, 4.0f * value));
            break;
        case LayerBlend::Min:
            EXPECT_FLOAT_EQ(height, std::fmin(a, 4.0f * value));
            break;
        case LayerBlend::Mask:
            EXPECT_FLOAT_EQ(height, a * 0.5f * (value + 1.0f));
            break;
        }
    }
}

TEST(EvaluateNoiseLayers, FixedSizeBlockMatchesTheLayerArray) {
    std::array<NoiseLayer, 3> layers{Layer(1u, 10.0f), Layer(2u, 4.0f, LayerBlend::Max),
                                     Layer(3u, 2.0f, LayerBlend::Mask)};
    layers[1].enabled = false;
    layers[2].warpAmplitude = 5.0f;
    terraingen::NoiseLayers block;
    for (const NoiseLayer& layer : layers) {
        block.layers[block.count++] = layer;
    }
    for (int i = 0; i < 20; ++i) {
        const float x = kX + 7.3f * static_cast<float>(i);
        EXPECT_EQ(EvaluateNoiseLayers(x, kY, block),
                  EvaluateNoiseLayers(x, kY, layers.data(), layers.size()));
    }
    block.count = 0;
    EXPECT_EQ(EvaluateNoiseLayers(kX, kY, block), 0.0f);
}

TEST(NoiseLayer, ComparesEveryField) {
    const NoiseLayer base;
    EXPECT_EQ(base, NoiseLayer{});
    NoiseLayer changed = base;
    changed.enabled = false;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.blend = LayerBlend::Min;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.warpAmplitude = 1.0f;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.warpWavelength = 1.0f;
    EXPECT_FALSE(changed == base);
    changed = base;
    changed.fractal.seed = 9u;
    EXPECT_FALSE(changed == base);
}

TEST(NoiseLayer, FbmSetsTheSeedWavelengthAndAmplitude) {
    const terraingen::NoiseLayer layer = terraingen::NoiseLayer::Fbm(3, 800.0, 40.0);
    terraingen::NoiseLayer expected;
    expected.fractal.seed = 3;
    expected.fractal.wavelength = 800.0f;
    expected.fractal.amplitude = 40.0f;
    EXPECT_EQ(layer, expected);
}
