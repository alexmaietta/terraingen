#include "terraingen/config/config.h"

namespace terraingen {

namespace {

/// Each layer of `stack` with its Hurst exponent applied, appended to `layers`.
template <typename Layers>
void AppendEffective(const std::vector<NoiseLayerConfig>& stack, Layers& layers) {
    for (const NoiseLayerConfig& layer : stack) {
        layers.push_back(layer.Effective());
    }
}

}  // namespace

NoiseLayer NoiseLayerConfig::Effective() const {
    NoiseLayer effective = layer;
    if (hurst) {
        effective.fractal.gain = GainFromHurst(*hurst, layer.fractal.lacunarity);
    }
    return effective;
}

SyntheticTerrainParams NoiseLayerSpec::Params() const {
    SyntheticTerrainParams params;
    params.layers.clear();
    AppendEffective(stack, params.layers);
    return params;
}

SyntheticAlbedoOptions SyntheticAlbedoSpec::Params() const {
    SyntheticAlbedoOptions params;
    params.rgb = rgb;
    params.hue = hue;
    params.saturation = saturation;
    params.value = value;
    AppendEffective(hueNoise, params.hueNoise);
    AppendEffective(saturationNoise, params.saturationNoise);
    AppendEffective(valueNoise, params.valueNoise);
    return params;
}

}  // namespace terraingen
