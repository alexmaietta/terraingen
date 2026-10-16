/// @file
/// @brief Tests of the configuration spec methods (pipeline/config.cpp).
#include "terraingen/pipeline/config.h"

#include <gtest/gtest.h>

#include <utility>
#include <variant>

using terraingen::LayerSpec;
using terraingen::NoiseLayerConfig;

TEST(Boxed, CopiesAndComparesByValue) {
    terraingen::Boxed<LayerSpec> a = terraingen::ScaleLayerSpec{2.0, 0.0};
    terraingen::Boxed<LayerSpec> b;
    EXPECT_TRUE(std::holds_alternative<terraingen::NoiseLayerSpec>(*b));
    b = a;
    EXPECT_EQ(a, b);
    std::get<terraingen::ScaleLayerSpec>(*b).scale = 3.0;
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*a).scale, 2.0) << "deep copies";
    EXPECT_FALSE(a == b);
    const terraingen::Boxed<LayerSpec> c = std::move(b);
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*c).scale, 3.0);
    EXPECT_EQ(c->index(), a->index());
}

TEST(NoiseLayerConfig, DerivesTheGainFromTheHurstExponent) {
    NoiseLayerConfig config;
    config.layer.fractal.gain = 0.3f;
    EXPECT_EQ(config.Effective().fractal.gain, 0.3f);
    config.hurst = 0.6f;
    EXPECT_EQ(config.Effective().fractal.gain,
              terraingen::GainFromHurst(0.6f, config.layer.fractal.lacunarity));
}

TEST(NoiseLayerSpec, GivesEachLayerOfTheStackWithItsHurstExponent) {
    terraingen::NoiseLayerSpec spec;
    spec.stack.push_back(NoiseLayerConfig{{}, 0.8f});
    const terraingen::SyntheticTerrainParams params = spec.Params();
    ASSERT_EQ(params.layers.size(), 2u);
    EXPECT_EQ(params.layers[0], spec.stack[0].layer);
    EXPECT_EQ(params.layers[1], spec.stack[1].Effective());
}

TEST(SyntheticAlbedoSpec, GivesTheColourAndEachNoiseStack) {
    terraingen::SyntheticAlbedoSpec spec;
    spec.rgb = true;
    spec.hue = 30.0;
    spec.saturation = 0.2;
    spec.value = 0.4;
    spec.hueNoise = {NoiseLayerConfig{}};
    spec.valueNoise = {NoiseLayerConfig{{}, 0.5f}, NoiseLayerConfig{}};
    const terraingen::SyntheticAlbedoParams params = spec.Params();
    EXPECT_TRUE(params.rgb);
    EXPECT_EQ(params.hue, 30.0);
    EXPECT_EQ(params.saturation, 0.2);
    EXPECT_EQ(params.value, 0.4);
    EXPECT_EQ(params.hueNoise.size(), 1u);
    EXPECT_TRUE(params.saturationNoise.empty());
    ASSERT_EQ(params.valueNoise.size(), 2u);
    EXPECT_EQ(params.valueNoise[0], spec.valueNoise[0].Effective());
}
