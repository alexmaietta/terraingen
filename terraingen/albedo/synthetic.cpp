/// @file
/// @brief Synthetic albedos: a base colour with noise in hue, saturation and value.
#include "terraingen/albedo/synthetic.h"

#include "terraingen/layers/noise_relief.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Checks one noise stack's size and layers.
void ValidateStack(const std::vector<NoiseLayer>& layers) {
    if (layers.size() > kMaxNoiseLayers) {
        throw std::invalid_argument("Too many albedo noise layers");
    }
    for (const NoiseLayer& layer : layers) {
        ValidateNoiseLayer(layer);
    }
}

}  // namespace

void ValidateSyntheticAlbedo(const SyntheticAlbedoParams& params) {
    RequireFinite(params.hue, "The albedo hue");
    if (!(IsInUnitInterval(params.saturation) && IsInUnitInterval(params.value))) {
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

SyntheticAlbedo::SyntheticAlbedo(SyntheticAlbedoParams params, Vec2d origin, Surface surface)
    : params_(std::move(params)), origin_(origin), surface_(surface) {
    ValidateSyntheticAlbedo(params_);
    if (surface_.IsSphere()) {
        for (const auto* stack :
             {&params_.hueNoise, &params_.saturationNoise, &params_.valueNoise}) {
            for (const NoiseLayer& layer : *stack) {
                if (!HasBasisNoise3(layer.fractal.basis)) {
                    throw std::invalid_argument(
                        "Albedo noise on a sphere needs a basis with a 3D form: perlin or value");
                }
            }
        }
    }
}

double SyntheticAlbedo::Noise(const std::vector<NoiseLayer>& layers, double x, double y) const {
    if (layers.empty()) {
        return 0.0;
    }
    return static_cast<double>(NoiseLayersAt(layers, origin_, surface_, x, y));
}

AlbedoSample SyntheticAlbedo::ValueAt(double x, double y, const AlbedoContext& /*context*/) const {
    const double value =
        std::clamp(params_.value * (1.0 + Noise(params_.valueNoise, x, y)), 0.0, 1.0);
    if (!params_.rgb) {
        return {static_cast<float>(value), 0.0f, 0.0f};
    }
    const double hue = params_.hue + Noise(params_.hueNoise, x, y);
    const double saturation =
        std::clamp(params_.saturation + Noise(params_.saturationNoise, x, y), 0.0, 1.0);
    return HsvToRgb(hue, saturation, value);
}

}  // namespace terraingen
