// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/validate.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

void ValidateFractalParams(const FractalParams& params) {
    RequirePositive(static_cast<double>(params.wavelength), "Fractal wavelength");
    RequireFinite(static_cast<double>(params.amplitude), "Fractal amplitude");
    if (params.octaves < 1 || params.octaves > 32) {
        throw std::invalid_argument("Fractal octaves must be between 1 and 32");
    }
    if (!(std::isfinite(params.lacunarity) && params.lacunarity > 1.0f)) {
        throw std::invalid_argument("Fractal lacunarity must be greater than 1 and finite");
    }
    RequirePositive(static_cast<double>(params.gain), "Fractal gain");
    if (static_cast<std::uint32_t>(params.basis) >= kNoiseBasisCount) {
        throw std::invalid_argument("Unknown noise basis");
    }
    if (static_cast<std::uint32_t>(params.type) >= kFractalTypeCount) {
        throw std::invalid_argument("Unknown fractal type");
    }
    RequirePositive(static_cast<double>(params.offset), "Fractal offset");
}

void ValidateFractal(const Fractal& fractal) {
    ValidateFractalParams(FractalParamsFrom(fractal));
    if (static_cast<std::uint32_t>(fractal.blend) >= kFractalBlendCount) {
        throw std::invalid_argument("Unknown layer blend mode");
    }
    RequireNonNegative(static_cast<double>(fractal.warpAmplitude), "Warp amplitude");
    RequirePositive(static_cast<double>(fractal.warpWavelength), "Warp wavelength");
}

void ValidateFractals(const std::vector<Fractal>& fractals, std::size_t minFractals) {
    if (fractals.size() < minFractals || fractals.size() > kMaxFractals) {
        throw std::invalid_argument("A noise stack needs between " + std::to_string(minFractals) +
                                    " and " + std::to_string(kMaxFractals) + " layers");
    }
    for (std::size_t i = 0; i < fractals.size(); ++i) {
        try {
            ValidateFractal(fractals[i]);
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Layer " + std::to_string(i + 1) + ": " + error.what());
        }
    }
}

}  // namespace terraingen
