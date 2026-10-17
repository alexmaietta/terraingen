/// @file
/// @brief Running a kernel over a grid on a GPU backend.
#pragma once

#include "terraingen/gpu/backend.h"
#include "terraingen/gpu/program.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <cstddef>
#include <span>

namespace terraingen {

/// Runs a kernel compiled by `terraingen_add_kernel` over every cell of `grid` holding data, in
/// strips of rows, on `backend` (`Vulkan`, `Cuda` or `Metal`).
///
/// Each cell gets the kernel's `ApplyCell` at its centre measured from `origin`, which the host
/// works out per column and per row in double, so cells see the same positions as on the CPU.
/// @param program The compiled kernel.
/// @param kernel The kernel's parameter block, laid out as in the kernel header.
/// @return false, leaving `grid` unchanged, when the backend is not a GPU one, its device is
/// unavailable, the build has no form of the kernel for it, or the grid cannot run there
/// (rotated, or wider than one dispatch reaches).
bool RunKernel(Backend backend, const KernelProgram& program, std::span<const std::byte> kernel,
               Raster<float>& grid, const Vec2d& origin);

}  // namespace terraingen
