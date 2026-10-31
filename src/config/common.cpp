// SPDX-License-Identifier: BSD-3-Clause

#include "config/common.h"

namespace terraingen {

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

namespace config {

namespace {

/// The most craters or candidate boulders a layer may be expected to draw over one output, as
/// `CraterGenerationOptions::maxExpectedCraters` caps craters.
constexpr double kMaxExpectedCount = 5e7;

}  // namespace

std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory) {
    const std::filesystem::path resolved = path;
    return resolved.is_relative() && !directory.empty() ? directory / resolved : resolved;
}

void CheckGridSize(std::size_t width, std::size_t height, const std::string& path) {
    const auto fits = [](std::size_t side) {
        return side >= kMinConfigGridSize && side <= kMaxConfigGridSize;
    };
    if (!fits(width) || !fits(height)) {
        throw ConfigError(path, std::to_string(width) + "x" + std::to_string(height) +
                                    " cells; each side must be " +
                                    CountText(kMinConfigGridSize, kMaxConfigGridSize));
    }
}

void CheckBand(std::size_t band, const GeoTiffInfo& info, const std::string& path) {
    if (band >= info.bands) {
        throw ConfigError(path, "the file has " + std::to_string(info.bands) +
                                    (info.bands == 1 ? " band" : " bands"));
    }
}

void CheckExpectedCount(double expected, std::string_view items, std::string_view remedy,
                        const std::string& path) {
    if (expected > kMaxExpectedCount) {
        throw ConfigError(path, "about " + std::to_string(static_cast<long long>(expected)) + " " +
                                    std::string(items) + " expected, more than the limit of " +
                                    std::to_string(static_cast<long long>(kMaxExpectedCount)) +
                                    "; " + std::string(remedy));
    }
}

std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory) {
    if (spec.path.empty()) {
        return std::make_shared<ConstantField>(spec.value);
    }
    return AtFile(
        path, [&] { return ReadRasterField(Resolve(spec.path, directory), spec.read, spec.band); });
}

}  // namespace config

}  // namespace terraingen
