#include "terraingen/albedo/bands.h"

#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/fade.h"
#include "terraingen/noise/fractal.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

void ScaleBands(AlbedoSample& value, std::size_t bands, double factor) {
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(factor * static_cast<double>(value[band]));
        }
    }
}

void ChangeBands(AlbedoSample& value, std::size_t bands, double change) {
    if (change != 0.0) {
        ScaleBands(value, bands, std::fmax(0.0, 1.0 + change));
    }
}

void MatureBands(AlbedoSample& value, std::size_t bands, double reference, double maturity,
                 double originReflectance, double factor) {
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(
                factor * ReflectanceAtMaturity(static_cast<double>(value[band]), reference,
                                               maturity, originReflectance));
        }
    }
}

double SmoothRamp(double t, double start, double width) {
    return Smoothstep((t - start) / width);
}

double FadeOut(double t, double start, double width) {
    return 1.0 - SmoothRamp(t, start, width);
}

double SoftEdge(double t, double feather) {
    return FadeOut(t, 1.0 - feather, feather);
}

double RingNoise(double ring, double angle, double z, std::uint32_t seed, int octaves) {
    FractalParams params;
    params.seed = seed;
    params.wavelength = 1.0f;
    params.amplitude = 1.0f;
    params.octaves = octaves;
    return static_cast<double>(Fractal3(ring * std::cos(angle), ring * std::sin(angle), z, params));
}

double EdgeLobe(double raggedness, double ring, double angle, double z, std::uint32_t seed,
                int octaves) {
    if (!(raggedness > 0.0)) {
        return 1.0;
    }
    return 1.0 - raggedness * std::clamp(0.5 - RingNoise(ring, angle, z, seed, octaves), 0.0, 1.0);
}

void ValidateRelativeChange(const ValueDistribution& change, const std::string& what) {
    ValidateValueDistribution(change);
    if (!(change.Lowest() > -1.0 && std::isfinite(change.Highest()))) {
        throw std::invalid_argument(what + " must be above -1 and bounded");
    }
}

double LifeLeft(double ageGyr, double lifetimeMyr) {
    return 1.0 - 1000.0 * ageGyr / lifetimeMyr;
}

}  // namespace terraingen
