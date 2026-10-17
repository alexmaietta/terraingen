/// @file
/// @brief `ApplyKernel`, applying a kernel on whichever backend a layer context gives.
#pragma once

#include "terraingen/gpu/kernel.h"
#include "terraingen/layers/kernels/cell.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/cell_loops.h"

#include <span>

namespace terraingen {

/// The position a kernel sees for surface point (x, y): measured from `origin`, in single
/// precision.
inline CellPosition MakeCellPosition(double x, double y, const Vec2d& origin) {
    CellPosition position;
    position.x = static_cast<float>(x - origin.x);
    position.y = static_cast<float>(y - origin.y);
    return position;
}

/// Applies `kernel` (`ApplyCell` in its kernel header) to every sample holding data.
///
/// A grid runs on the context's GPU backend when it has one and `RunKernel` can take the grid,
/// with the kernel `terraingen_add_kernel` compiled (`CompiledKernel`), otherwise on the CPU over
/// `context.threads`; points always run on the CPU. The CPU and the GPU evaluate the same kernel
/// header at the same positions, so they agree to within float rounding.
template <typename Kernel>
void ApplyKernel(SampleBlock& samples, const LayerContext& context, const Kernel& kernel,
                 const Vec2d& origin) {
    if (Raster<float>* grid = samples.Grid()) {
        if (context.backend != Backend::Cpu &&
            RunKernel(context.backend, CompiledKernel<Kernel>(),
                      std::as_bytes(std::span(&kernel, 1)), *grid, origin)) {
            return;
        }
        MapCells(*grid, context.threads, [&](float height, const Vec2d& centre) {
            return ApplyCell(kernel, height, MakeCellPosition(centre.x, centre.y, origin));
        });
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i)) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) =
                ApplyCell(kernel, samples.Height(i), MakeCellPosition(p.x, p.y, origin));
        }
    }
}

}  // namespace terraingen
