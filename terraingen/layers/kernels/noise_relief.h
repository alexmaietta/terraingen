/// @file
/// @brief The kernel of `NoiseRelief`; a kernel header for C++ and Slang.
#pragma once

#include "terraingen/layers/kernels/cell.h"
#include "terraingen/noise/noise_layers.h"

#ifndef __SLANG__
#include "terraingen/gpu/program.h"

#include <vector>
#endif

namespace terraingen {

/// The enabled layers of a noise relief, in order.
struct NoiseReliefKernel {
    NoiseLayers layers;  ///< Only enabled layers, so every `enabled` flag is set.
};

#ifdef __SLANG__
/// A cell's height after the noise relief: `height` plus the stacked layers at `position`.
inline float ApplyCell(NoiseReliefKernel kernel, float height, CellPosition position) {
    return height + EvaluateNoiseLayers(position.x, position.y, kernel.layers);
}
#else
/// A cell's height after the noise relief: `height` plus the stacked layers at `position`. The
/// same stack as on the GPU, taken by reference so no cell copies the block.
inline float ApplyCell(const NoiseReliefKernel& kernel, float height, CellPosition position) {
    return height +
           EvaluateNoiseLayers(position.x, position.y, kernel.layers.layers, kernel.layers.count);
}

static_assert(sizeof(NoiseReliefKernel) == 420, "NoiseReliefKernel must match its std430 layout");

/// The compiled forms of the noise relief kernel.
template <> KernelProgram CompiledKernel<NoiseReliefKernel>();

/// The kernel of `layers`, at most `kMaxNoiseLayers`, keeping the enabled ones in order; skipping
/// a disabled layer leaves the stack as it was, so the heights are the same.
inline NoiseReliefKernel MakeNoiseReliefKernel(const std::vector<NoiseLayer>& layers) {
    NoiseReliefKernel kernel;
    for (const NoiseLayer& layer : layers) {
        if (layer.enabled && kernel.layers.count < kMaxNoiseLayers) {
            kernel.layers.layers[kernel.layers.count++] = layer;
        }
    }
    return kernel;
}
#endif

}  // namespace terraingen
