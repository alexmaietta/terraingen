/// @file
/// @brief Host-side checks of fractal and noise layer parameters before kernels use them.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/noise_layers.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// Checks that `params` describe a usable fractal.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateFractalParams(const FractalParams& params);

/// Checks a layer's fractal, blend mode and domain warp.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateNoiseLayer(const NoiseLayer& layer);

/// Checks a stack of `minLayers` to `kMaxNoiseLayers` layers and each layer in it.
/// @throws std::invalid_argument for too few or too many layers, or for an invalid layer, its
/// message starting "Layer n: " with n counted from 1.
void ValidateNoiseLayers(const std::vector<NoiseLayer>& layers, std::size_t minLayers = 0);

}  // namespace terraingen
