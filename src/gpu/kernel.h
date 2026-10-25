// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Running a kernel over a grid on a GPU backend.
#pragma once

#include "terraingen/gpu/backend.h"
#include "terraingen/gpu/program.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/span.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// A read-only storage buffer a kernel binds after its parameter block: host bytes and the element
/// count its structured buffer reports.
struct KernelBuffer {
    Span<const std::byte> bytes;  ///< The host data.
    std::size_t count = 0;        ///< Number of elements.
    /// Elements per grid cell, row by row, for a buffer bound one strip of rows at a time like
    /// the heights; 0 for a buffer bound whole in every strip.
    std::size_t perCell = 0;
};

/// The buffer of `values`, which must outlive it.
/// @param perCell As `KernelBuffer::perCell`.
template <typename T>
KernelBuffer MakeKernelBuffer(const std::vector<T>& values, std::size_t perCell = 0) {
    return {AsBytes(Span(values)), values.size(), perCell};
}

/// Runs a kernel compiled by `terraingen_add_kernel` over every cell of `grid` holding data, in
/// strips of rows, on `backend` (`Vulkan`, `Cuda` or `Metal`).
///
/// Each cell gets the kernel's `ApplyCell` at its center measured from `origin`, which the host
/// works out per column and per row in double, so cells see the same positions as on the CPU.
/// @param kernel The kernel's parameter block, laid out as in the kernel header.
/// @param extra Further buffers, bound in order after the parameter block: whole in every strip,
/// or the strip's rows of a buffer with `perCell` elements for each of the grid's cells.
/// @return false, leaving `grid` unchanged, when the backend is not a GPU one, its device is
/// unavailable, the build has no form of the kernel for it, or the grid cannot run there
/// (rotated, or wider than one dispatch reaches).
bool RunKernel(Backend backend, const KernelProgram& program, Span<const std::byte> kernel,
               Raster<float>& grid, const Vec2d& origin, Span<const KernelBuffer> extra = {});

}  // namespace terraingen
