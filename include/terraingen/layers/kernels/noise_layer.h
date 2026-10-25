// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The kernel of `NoiseLayer`; a kernel header for C++ and Slang.
#pragma once

#include "terraingen/layers/kernels/cell.h"
#include "terraingen/noise/fractals.h"

#ifndef __SLANG__
#include "terraingen/gpu/program.h"

#include <vector>
#endif

namespace terraingen {

/// The enabled fractals of a noise layer, in order.
struct NoiseLayerKernel {
    FractalStack stack;  ///< Only enabled fractals, so every `enabled` flag is set.
};

#ifdef __SLANG__
/// A cell's height after the noise layer: `height` plus the stacked fractals at `position`.
inline float ApplyCell(NoiseLayerKernel kernel, float height, CellPosition position) {
    return height + EvaluateFractals(position.x, position.y, kernel.stack);
}
#else
/// A cell's height after the noise layer: `height` plus the stacked fractals at `position`. The
/// same stack as on the GPU, taken by reference so no cell copies the block.
inline float ApplyCell(const NoiseLayerKernel& kernel, float height, CellPosition position) {
    return height +
           EvaluateFractals(position.x, position.y, kernel.stack.fractals, kernel.stack.count);
}

static_assert(sizeof(NoiseLayerKernel) == 420, "NoiseLayerKernel must match its std430 layout");

/// The compiled forms of the noise layer kernel.
template <> KernelProgram CompiledKernel<NoiseLayerKernel>();

/// The kernel of `fractals`, at most `kMaxFractals`, keeping the enabled ones in order; skipping
/// a disabled fractal leaves the stack as it was, so the heights are the same.
inline NoiseLayerKernel MakeNoiseLayerKernel(const std::vector<Fractal>& fractals) {
    NoiseLayerKernel kernel;
    for (const Fractal& fractal : fractals) {
        if (fractal.enabled && kernel.stack.count < kMaxFractals) {
            kernel.stack.fractals[kernel.stack.count++] = fractal;
        }
    }
    return kernel;
}
#endif

}  // namespace terraingen
