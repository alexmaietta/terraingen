// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `KernelWeight`, the unnormalized weight of an interpolation kernel.
#pragma once

#include "terraingen/raster/resample.h"

namespace terraingen {

/// The weight of a cubic, Lanczos or B-spline kernel at an offset of `x` cells, before any
/// normalization.
double KernelWeight(ResampleMethod method, double x);

}  // namespace terraingen
