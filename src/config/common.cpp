// SPDX-License-Identifier: BSD-3-Clause

#include "config/common.h"

#include "terraingen/layers/craters/generation/population.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory) {
    const std::filesystem::path resolved = path;
    return resolved.is_relative() && !directory.empty() ? directory / resolved : resolved;
}

double DegreesFromRadians(double radians) {
    const double nearest = radians * kRadian;
    if (!std::isfinite(nearest)) {
        return nearest;
    }
    std::array<char, 32> text{};
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(text.data(), text.size(), "%.*g", digits, nearest);
        const double degrees = std::strtod(text.data(), nullptr);
        if (RadiansFromDegrees(degrees) == radians) {
            return degrees;
        }
    }
    return nearest;
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
    const double limit = CraterGenerationOptions{}.maxExpectedCraters;
    if (expected > limit) {
        throw ConfigError(path, "about " + std::to_string(static_cast<long long>(expected)) + " " +
                                    std::string(items) + " expected, more than the limit of " +
                                    std::to_string(static_cast<long long>(limit)) + "; " +
                                    std::string(remedy));
    }
}

std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory) {
    if (spec.path.empty()) {
        return std::make_shared<ConstantField>(spec.value);
    }
    RasterFieldOptions read;
    read.method = spec.method;
    read.scale = spec.scale;
    read.fallback = spec.fallback.value_or(std::numeric_limits<double>::quiet_NaN());
    read.periodicLongitude = spec.periodicLongitude;
    return AtFile(path,
                  [&] { return ReadRasterField(Resolve(spec.path, directory), read, spec.band); });
}

DensityMap BuildDensityMap(const DensityMapSpec& spec, const std::string& path,
                           const std::filesystem::path& directory) {
    DensityMap map = spec.map;
    map.field = BuildField(spec.field, Child(path, "field"), directory);
    At(path, [&] { ValidateDensityMap(map); });
    return map;
}

SizeSlopeMap BuildSizeSlopeMap(const SizeSlopeMapSpec& spec, const std::string& path,
                               const std::filesystem::path& directory) {
    SizeSlopeMap map = spec.map;
    map.field = BuildField(spec.field, Child(path, "field"), directory);
    At(path, [&] { ValidateSizeSlopeMap(map); });
    return map;
}

}  // namespace config

}  // namespace terraingen
