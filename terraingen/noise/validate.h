// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Host-side checks of fractal parameters and fractal stacks before kernels use them.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/fractals.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// Checks that `params` describe a usable fractal.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateFractalParams(const FractalParams& params);

/// Checks a fractal's parameters, blend mode and domain warp.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateFractal(const Fractal& fractal);

/// Checks a stack of `minFractals` to `kMaxFractals` fractals and each fractal in it.
/// @throws std::invalid_argument for too few or too many fractals, or for an invalid fractal, its
/// message starting "Layer n: " with n counted from 1.
void ValidateFractals(const std::vector<Fractal>& fractals, std::size_t minFractals = 0);

}  // namespace terraingen
