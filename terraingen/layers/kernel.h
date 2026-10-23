// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ApplyKernel`, applying a kernel on whichever backend a layer context gives.
#pragma once

#include "terraingen/gpu/kernel.h"
#include "terraingen/layers/kernels/cell.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/span.h"

namespace terraingen {

/// Applies `kernel` (`ApplyCell` in its kernel header) to every sample holding data.
///
/// A grid runs on the context's GPU backend when it has one and `RunKernel` can take the grid,
/// with the kernel `terraingen_add_kernel` compiled (`CompiledKernel`), otherwise on the CPU over
/// `context.threads`; points always run on the CPU. The CPU and the GPU evaluate the same kernel
/// header at the same positions, so they agree to within float rounding.
template <typename Kernel>
void ApplyKernel(SampleBlock& samples, const LayerContext& context, const Kernel& kernel,
                 const Vec2d& origin) {
    Raster<float>* grid = samples.Grid();
    if (grid != nullptr && context.backend != Backend::Cpu &&
        RunKernel(context.backend, CompiledKernel<Kernel>(), AsBytes(Span(&kernel, 1)), *grid,
                  origin)) {
        return;
    }
    MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
        return ApplyCell(
            kernel, height,
            CellPosition{static_cast<float>(p.x - origin.x), static_cast<float>(p.y - origin.y)});
    });
}

}  // namespace terraingen
