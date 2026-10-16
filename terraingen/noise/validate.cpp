#include "terraingen/noise/validate.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

void ValidateFractalParams(const FractalParams& params) {
    RequirePositive(params.wavelength, "Fractal wavelength");
    RequireFinite(params.amplitude, "Fractal amplitude");
    if (params.octaves < 1 || params.octaves > 32) {
        throw std::invalid_argument("Fractal octaves must be between 1 and 32");
    }
    if (!(std::isfinite(params.lacunarity) && params.lacunarity > 1.0f)) {
        throw std::invalid_argument("Fractal lacunarity must be greater than 1 and finite");
    }
    RequirePositive(params.gain, "Fractal gain");
    if (static_cast<std::uint32_t>(params.basis) >= kNoiseBasisCount) {
        throw std::invalid_argument("Unknown noise basis");
    }
    if (static_cast<std::uint32_t>(params.type) >= kFractalTypeCount) {
        throw std::invalid_argument("Unknown fractal type");
    }
    RequirePositive(params.offset, "Fractal offset");
}

void ValidateNoiseLayer(const NoiseLayer& layer) {
    ValidateFractalParams(layer.fractal);
    if (static_cast<std::uint32_t>(layer.blend) >= kLayerBlendCount) {
        throw std::invalid_argument("Unknown layer blend mode");
    }
    RequireNonNegative(layer.warpAmplitude, "Warp amplitude");
    RequirePositive(layer.warpWavelength, "Warp wavelength");
}

}  // namespace terraingen
