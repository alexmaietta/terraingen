// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/synthetic.h"

#include "terraingen/noise/surface_noise.h"
#include "terraingen/noise/validate.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

void ValidateSyntheticAlbedoOptions(const SyntheticAlbedoOptions& options) {
    RequireFinite(options.hueDegrees, "The albedo hue");
    if (!(IsInUnitInterval(options.saturation) && IsInUnitInterval(options.value))) {
        throw std::invalid_argument("The albedo saturation and value must be in [0, 1]");
    }
    ValidateFractals(options.hueNoiseDegrees);
    ValidateFractals(options.saturationNoise);
    ValidateFractals(options.valueNoise);
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

SyntheticAlbedo::SyntheticAlbedo(SyntheticAlbedoOptions options, TerrainBase base)
    : options_(std::move(options)), origin_(base.ReferencePoint()), surface_(base.surface) {
    ValidateSyntheticAlbedoOptions(options_);
}

double SyntheticAlbedo::Noise(const std::vector<Fractal>& fractals, double x, double y) const {
    return static_cast<double>(FractalStackAt(fractals, origin_, surface_, x, y));
}

AlbedoSample SyntheticAlbedo::ValueAt(double x, double y, const AlbedoContext& /*context*/) const {
    const double value =
        std::clamp(options_.value * (1.0 + Noise(options_.valueNoise, x, y)), 0.0, 1.0);
    if (!options_.rgb) {
        return {static_cast<float>(value), 0.0f, 0.0f};
    }
    const double hue = options_.hueDegrees + Noise(options_.hueNoiseDegrees, x, y);
    const double saturation =
        std::clamp(options_.saturation + Noise(options_.saturationNoise, x, y), 0.0, 1.0);
    return HsvToRgb(hue, saturation, value);
}

}  // namespace terraingen
