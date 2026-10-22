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

CraterApplyOptions DefaultCraterApply() {
    CraterApplyOptions apply;
    apply.irregularity.enabled = true;
    return apply;
}

BoulderLayerOptions DefaultBoulders() {
    BoulderLayerOptions boulders;
    boulders.sizes.maxDiameter =
        Sp8023IntercraterBlocks(SpecificationCurve::Nominal).ValidRange().max;
    return boulders;
}

RoughnessOptions DefaultRoughness() {
    RoughnessOptions roughness;
    roughness.maxWavelength = 16.0;
    return roughness;
}

BedformOptions DefaultBedforms() {
    BedformOptions bedforms;
    bedforms.wavelength = 4.0;
    bedforms.height = 0.4;
    bedforms.cover = 0.9;
    return bedforms;
}

DuneOptions DefaultDunes() {
    DuneOptions dunes;
    dunes.width = 50.0;
    dunes.minWidth = 15.0;
    dunes.density = 0.5;
    dunes.spacing = 129.0;
    return dunes;
}

NoiseLayer NoiseLayerConfig::Effective() const {
    NoiseLayer effective = layer;
    if (hurst) {
        effective.fractal.gain = GainFromHurst(*hurst, layer.fractal.lacunarity);
    }
    if (amplitude) {
        effective.fractal.amplitude = *amplitude;
    }
    return effective;
}

NoiseLayer NoiseLayerConfig::EffectiveRelief() const {
    NoiseLayer effective = Effective();
    if (!amplitude) {
        effective.fractal.amplitude = kNoiseAmplitudeRatio * layer.fractal.wavelength;
    }
    return effective;
}

SyntheticTerrainParams StackParams(const std::vector<NoiseLayerConfig>& stack) {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const NoiseLayerConfig& layer : stack) {
        params.layers.push_back(layer.EffectiveRelief());
    }
    return params;
}

SyntheticTerrainParams NoiseLayerSpec::Params() const {
    return StackParams(stack);
}

SyntheticTerrainParams NoiseLayerSpec::EstimatedParams(const BaseRoughness& base) const {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (NoiseLayerConfig layer : stack) {
        if (!layer.hurst) {
            layer.hurst = static_cast<float>(base.hurst);
        }
        NoiseLayer effective = layer.EffectiveRelief();
        if (!layer.amplitude && effective.enabled) {
            BaseRoughness continued = base;
            continued.hurst = static_cast<double>(*layer.hurst);
            effective.fractal.amplitude = MatchedNoiseAmplitude(effective, continued);
        }
        params.layers.push_back(effective);
    }
    return params;
}

RoughnessOptions RoughnessLayerSpec::Options() const {
    RoughnessOptions options = roughness;
    options.rmsSlope = rmsSlope.value_or(roughness.rmsSlope);
    options.hurst = hurst.value_or(roughness.hurst);
    return options;
}

RoughnessOptions RoughnessLayerSpec::EstimatedOptions(BaseRoughness base) const {
    RoughnessOptions options = Options();
    base.hurst = hurst.value_or(base.hurst);
    options.hurst = base.hurst;
    options.rmsSlope = rmsSlope.value_or(base.RmsSlope(options.baseline));
    return options;
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
