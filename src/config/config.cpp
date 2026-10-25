// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/config.h"

namespace terraingen {

namespace {

/// Each layer of `stack` with its Hurst exponent applied, appended to `layers`.
template <typename Spec, typename Layers>
void AppendEffective(const std::vector<Spec>& stack, Layers& layers) {
    for (const Spec& layer : stack) {
        layers.push_back(layer.Effective());
    }
}

}  // namespace

Fractal FractalSpec::Effective() const {
    Fractal effective = fractal;
    if (hurst) {
        effective.gain = GainFromHurst(*hurst, fractal.lacunarity);
    }
    if (amplitude) {
        effective.amplitude = *amplitude;
    }
    return effective;
}

Fractal FractalSpec::EffectiveRelief() const {
    Fractal effective = Effective();
    if (!amplitude) {
        effective.amplitude = kNoiseAmplitudeRatio * fractal.wavelength;
    }
    return effective;
}

std::vector<Fractal> StackFractals(const std::vector<FractalSpec>& stack) {
    std::vector<Fractal> fractals;
    for (const FractalSpec& spec : stack) {
        fractals.push_back(spec.EffectiveRelief());
    }
    return fractals;
}

std::vector<Fractal> NoiseLayerSpec::Fractals() const {
    return StackFractals(stack);
}

std::vector<Fractal> NoiseLayerSpec::EstimatedFractals(const BaseRoughness& source) const {
    std::vector<Fractal> fractals;
    for (FractalSpec layer : stack) {
        if (!layer.hurst) {
            layer.hurst = static_cast<float>(source.hurst);
        }
        Fractal effective = layer.EffectiveRelief();
        if (!layer.amplitude && effective.enabled) {
            BaseRoughness continued = source;
            continued.hurst = static_cast<double>(*layer.hurst);
            effective.amplitude = MatchedNoiseAmplitude(effective, continued);
        }
        fractals.push_back(effective);
    }
    return fractals;
}

RoughnessOptions RoughnessLayerSpec::Options() const {
    RoughnessOptions options = roughness;
    options.rmsSlope = rmsSlope.value_or(roughness.rmsSlope);
    options.hurst = hurst.value_or(roughness.hurst);
    return options;
}

RoughnessOptions RoughnessLayerSpec::EstimatedOptions(BaseRoughness source) const {
    RoughnessOptions options = Options();
    source.hurst = hurst.value_or(source.hurst);
    options.hurst = source.hurst;
    options.rmsSlope = rmsSlope.value_or(source.RmsSlope(options.baseline));
    return options;
}

SyntheticAlbedoOptions SyntheticAlbedoSpec::Options() const {
    SyntheticAlbedoOptions options;
    options.rgb = rgb;
    options.hueDegrees = hueDegrees;
    options.saturation = saturation;
    options.value = value;
    AppendEffective(hueNoiseDegrees, options.hueNoiseDegrees);
    AppendEffective(saturationNoise, options.saturationNoise);
    AppendEffective(valueNoise, options.valueNoise);
    return options;
}

SpectralContinuationOptions SpectralContinuationLayerSpec::Options() const {
    SpectralContinuationOptions options;
    options.pixelSize = pixelSize.value_or(options.pixelSize);
    options.slope = slope.value_or(options.slope);
    options.octaveRms = octaveRms.value_or(options.octaveRms);
    options.minWavelength = minWavelength;
    options.taperOctaves = taperOctaves;
    options.seed = seed;
    options.fade = fade;
    return options;
}

}  // namespace terraingen
