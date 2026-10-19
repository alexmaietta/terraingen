#include "terraingen/noise/validate.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

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

void ValidateNoiseLayer(const NoiseLayer& layer) {
    ValidateFractalParams(layer.fractal);
    if (static_cast<std::uint32_t>(layer.blend) >= kLayerBlendCount) {
        throw std::invalid_argument("Unknown layer blend mode");
    }
    RequireNonNegative(static_cast<double>(layer.warpAmplitude), "Warp amplitude");
    RequirePositive(static_cast<double>(layer.warpWavelength), "Warp wavelength");
}

}  // namespace terraingen
