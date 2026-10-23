#include "terraingen/config/config.h"

namespace terraingen {

namespace {

/// Each layer of `stack` with its Hurst exponent applied, appended to `layers`.
template <typename Layers>
void AppendEffective(const std::vector<NoiseStackLayerSpec>& stack, Layers& layers) {
    for (const NoiseStackLayerSpec& layer : stack) {
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

NoiseLayer NoiseStackLayerSpec::Effective() const {
    NoiseLayer effective = layer;
    if (hurst) {
        effective.fractal.gain = GainFromHurst(*hurst, layer.fractal.lacunarity);
    }
    if (amplitude) {
        effective.fractal.amplitude = *amplitude;
    }
    return effective;
}

NoiseLayer NoiseStackLayerSpec::EffectiveRelief() const {
    NoiseLayer effective = Effective();
    if (!amplitude) {
        effective.fractal.amplitude = kNoiseAmplitudeRatio * layer.fractal.wavelength;
    }
    return effective;
}

SyntheticTerrainParams StackParams(const std::vector<NoiseStackLayerSpec>& stack) {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const NoiseStackLayerSpec& layer : stack) {
        params.layers.push_back(layer.EffectiveRelief());
    }
    return params;
}

SyntheticTerrainParams NoiseLayerSpec::Params() const {
    return StackParams(stack);
}

SyntheticTerrainParams NoiseLayerSpec::EstimatedParams(const BaseRoughness& source) const {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (NoiseStackLayerSpec layer : stack) {
        if (!layer.hurst) {
            layer.hurst = static_cast<float>(source.hurst);
        }
        NoiseLayer effective = layer.EffectiveRelief();
        if (!layer.amplitude && effective.enabled) {
            BaseRoughness continued = source;
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
    options.hue = hue;
    options.saturation = saturation;
    options.value = value;
    AppendEffective(hueNoise, options.hueNoise);
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
