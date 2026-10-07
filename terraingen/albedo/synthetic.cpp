#include "terraingen/albedo/synthetic.h"

#include "terraingen/sources/synthetic_terrain.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

void ValidateStack(const std::vector<NoiseLayer>& layers) {
    if (layers.size() > kMaxNoiseLayers) {
        throw std::invalid_argument("Too many albedo noise layers");
    }
    for (const NoiseLayer& layer : layers) {
        ValidateNoiseLayer(layer);
    }
}

double Noise(const std::vector<NoiseLayer>& layers, float x, float y) {
    return layers.empty()
               ? 0.0
               : static_cast<double>(EvaluateNoiseLayers(x, y, layers.data(), layers.size()));
}

}  // namespace

void ValidateSyntheticAlbedo(const SyntheticAlbedoParams& params) {
    if (!std::isfinite(params.hue)) {
        throw std::invalid_argument("The albedo hue must be finite");
    }
    if (!(params.saturation >= 0.0 && params.saturation <= 1.0 && params.value >= 0.0 &&
          params.value <= 1.0)) {
        throw std::invalid_argument("The albedo saturation and value must be in [0, 1]");
    }
    ValidateStack(params.hueNoise);
    ValidateStack(params.saturationNoise);
    ValidateStack(params.valueNoise);
}

AlbedoSample HsvToRgb(double hue, double saturation, double value) {
    const double h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0) / 60.0;
    const double sector = std::floor(h);
    const double f = h - sector;
    const auto p = static_cast<float>(value * (1.0 - saturation));
    const auto q = static_cast<float>(value * (1.0 - saturation * f));
    const auto t = static_cast<float>(value * (1.0 - saturation * (1.0 - f)));
    const auto v = static_cast<float>(value);
    switch (static_cast<int>(sector)) {
    case 0:
        return {v, t, p};
    case 1:
        return {q, v, p};
    case 2:
        return {p, v, t};
    case 3:
        return {p, q, v};
    case 4:
        return {t, p, v};
    default:
        return {v, p, q};
    }
}

SyntheticAlbedo::SyntheticAlbedo(SyntheticAlbedoParams params, Vec2d origin)
    : params_(std::move(params)), origin_(origin) {
    ValidateSyntheticAlbedo(params_);
}

AlbedoSample SyntheticAlbedo::ValueAt(double x, double y, const AlbedoContext& /*context*/) const {
    const auto fx = static_cast<float>(x - origin_.x);
    const auto fy = static_cast<float>(y - origin_.y);
    const double value =
        std::clamp(params_.value * (1.0 + Noise(params_.valueNoise, fx, fy)), 0.0, 1.0);
    if (!params_.rgb) {
        return {static_cast<float>(value), 0.0f, 0.0f};
    }
    const double hue = params_.hue + Noise(params_.hueNoise, fx, fy);
    const double saturation =
        std::clamp(params_.saturation + Noise(params_.saturationNoise, fx, fy), 0.0, 1.0);
    return HsvToRgb(hue, saturation, value);
}

}  // namespace terraingen
