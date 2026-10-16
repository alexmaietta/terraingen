/// @file
/// @brief Host-side checks of fractal and noise layer parameters before kernels use them.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/noise_layers.h"

namespace terraingen {

/// @brief Checks that `params` describe a usable fractal.
/// @param params The fractal to check.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateFractalParams(const FractalParams& params);

/// @brief Checks a layer's fractal, blend mode and domain warp.
/// @param layer The layer to check.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateNoiseLayer(const NoiseLayer& layer);

}  // namespace terraingen
