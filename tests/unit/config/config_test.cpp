// SPDX-License-Identifier: BSD-3-Clause

#include "config/terrain.h"

#include "terraingen/config/json.h"

#include <gtest/gtest.h>

#include <utility>
#include <variant>

using terraingen::FractalSpec;
using terraingen::LayerSpec;

TEST(Boxed, CopiesByValue) {
    terraingen::Boxed<LayerSpec> a = terraingen::ScaleLayerSpec{2.0, 0.0};
    terraingen::Boxed<LayerSpec> b;
    EXPECT_TRUE(std::holds_alternative<terraingen::NoiseLayerSpec>(*b));
    b = a;
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*b).scale, 2.0);
    std::get<terraingen::ScaleLayerSpec>(*b).scale = 3.0;
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*a).scale, 2.0) << "deep copies";
    const terraingen::Boxed<LayerSpec> c = std::move(b);
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*c).scale, 3.0);
    EXPECT_EQ(c->index(), a->index());
}

TEST(ConfigSpec, ComparesAsItsJson) {
    terraingen::ConfigSpec a;
    a.layers = {terraingen::ScaleLayerSpec{2.0, 0.0}};
    terraingen::ConfigSpec b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    std::get<terraingen::ScaleLayerSpec>(b.layers[0]).offset = 1.0;
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(SpectralContinuationLayerSpec, GivesItsOptionsWithDefaultsForUnsetValues) {
    terraingen::SpectralContinuationLayerSpec spec;
    spec.minWavelength = 0.2;
    spec.seed = 7;
    const terraingen::SpectralContinuationOptions defaults;
    terraingen::SpectralContinuationOptions options = spec.Options();
    EXPECT_EQ(options.pixelSize, defaults.pixelSize);
    EXPECT_EQ(options.slope, defaults.slope);
    EXPECT_EQ(options.octaveRms, defaults.octaveRms);
    EXPECT_EQ(options.minWavelength, 0.2);
    EXPECT_EQ(options.seed, 7u);
    spec.pixelSize = 3.0;
    spec.slope = 2.5;
    spec.octaveRms = 0.4;
    options = spec.Options();
    EXPECT_EQ(options.pixelSize, 3.0);
    EXPECT_EQ(options.slope, 2.5);
    EXPECT_EQ(options.octaveRms, 0.4);
}

TEST(FractalSpec, DerivesTheGainFromTheHurstExponent) {
    FractalSpec config;
    config.fractal.gain = 0.3f;
    EXPECT_EQ(config.Effective().gain, 0.3f);
    config.hurst = 0.6f;
    EXPECT_EQ(config.Effective().gain, terraingen::GainFromHurst(0.6f, config.fractal.lacunarity));
}

TEST(NoiseLayerSpec, GivesEachLayerOfTheStackWithItsHurstExponent) {
    terraingen::NoiseLayerSpec spec;
    spec.stack.push_back(FractalSpec{{}, 0.8f, {}});
    const std::vector<terraingen::Fractal> params = spec.Fractals();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0], spec.stack[0].fractal);
    EXPECT_EQ(params[1], spec.stack[1].Effective());
}

namespace {

terraingen::BaseRoughness Base() {
    terraingen::BaseRoughness base;
    base.hurst = 0.6;
    base.deviation = 0.2;
    base.cellSize = 2.0;
    return base;
}

}  // namespace

TEST(NoiseLayerSpec, EstimatesUnsetHurstExponentsAndAmplitudesFromTheBase) {
    terraingen::NoiseLayerSpec spec;
    spec.stack[0].fractal.wavelength = 4.0f;
    spec.stack.push_back(FractalSpec{{}, 0.9f, 1.5f});
    spec.stack.push_back(FractalSpec{});
    spec.stack[2].fractal.enabled = false;
    const std::vector<terraingen::Fractal> params = spec.EstimatedFractals(Base());
    ASSERT_EQ(params.size(), 3u);
    const terraingen::Fractal& first = params[0];
    EXPECT_EQ(first.gain, terraingen::GainFromHurst(0.6f, first.lacunarity));
    EXPECT_EQ(first.amplitude, terraingen::MatchedNoiseAmplitude(first, Base()));
    EXPECT_EQ(params[1], spec.stack[1].Effective()) << "explicit values override";
    EXPECT_EQ(params[2].amplitude, spec.Fractals()[2].amplitude);

    FractalSpec amplitudeOnly;
    amplitudeOnly.hurst = 0.9f;
    spec.stack = {amplitudeOnly};
    terraingen::BaseRoughness continued = Base();
    continued.hurst = static_cast<double>(0.9f);
    const terraingen::Fractal layer = spec.EstimatedFractals(Base())[0];
    EXPECT_EQ(layer.amplitude, terraingen::MatchedNoiseAmplitude(layer, continued))
        << "the base continues at the layer's own Hurst exponent";
}

TEST(RoughnessLayerSpec, TakesTheDefaultsTheGivenValuesOrTheBaseEstimates) {
    terraingen::RoughnessLayerSpec spec;
    EXPECT_EQ(spec.Options(), terraingen::RoughnessOptions{});
    terraingen::RoughnessOptions estimated = spec.EstimatedOptions(Base());
    EXPECT_EQ(estimated.hurst, 0.6);
    EXPECT_DOUBLE_EQ(estimated.rmsSlope, Base().RmsSlope(spec.roughness.baseline));
    EXPECT_EQ(estimated.maxWavelength, spec.roughness.maxWavelength);

    spec.hurst = 0.9;
    terraingen::BaseRoughness continued = Base();
    continued.hurst = 0.9;
    estimated = spec.EstimatedOptions(Base());
    EXPECT_EQ(estimated.hurst, 0.9);
    EXPECT_DOUBLE_EQ(estimated.rmsSlope, continued.RmsSlope(spec.roughness.baseline));

    spec.rmsSlope = 0.05;
    EXPECT_EQ(spec.EstimatedOptions(Base()).rmsSlope, 0.05);
    EXPECT_EQ(spec.Options().rmsSlope, 0.05);
    EXPECT_EQ(spec.Options().hurst, 0.9);
}

TEST(SyntheticAlbedoSpec, GivesTheColorAndEachNoiseStack) {
    terraingen::SyntheticAlbedoSpec spec;
    spec.rgb = true;
    spec.hueDegrees = 30.0;
    spec.saturation = 0.2;
    spec.value = 0.4;
    spec.hueNoiseDegrees = {terraingen::AlbedoFractalSpec{}};
    spec.valueNoise = {terraingen::AlbedoFractalSpec{{{}, 0.5f, {}}},
                       terraingen::AlbedoFractalSpec{}};
    const terraingen::SyntheticAlbedoOptions params = spec.Options();
    EXPECT_TRUE(params.rgb);
    EXPECT_EQ(params.hueDegrees, 30.0);
    EXPECT_EQ(params.saturation, 0.2);
    EXPECT_EQ(params.value, 0.4);
    EXPECT_EQ(params.hueNoiseDegrees.size(), 1u);
    EXPECT_TRUE(params.saturationNoise.empty());
    ASSERT_EQ(params.valueNoise.size(), 2u);
    EXPECT_EQ(params.valueNoise[0], spec.valueNoise[0].Effective());
}
