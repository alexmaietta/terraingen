/// @file
/// @brief Running a cell kernel over a grid on the Vulkan device.
#pragma once

#include "terraingen/gpu/vulkan.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <cstddef>
#include <span>

namespace terraingen {

/// Runs a cell kernel compiled by `terraingen_add_cell_kernel` over every cell of `grid` holding
/// data, in strips of rows.
///
/// Each cell gets the kernel's `ApplyCell` at its centre measured from `origin`, which the host
/// works out per column and per row in double, so cells see the same positions as on the CPU.
/// @param program The compiled kernel.
/// @param kernel The kernel's parameter block, laid out as in the kernel header.
/// @return false, leaving `grid` unchanged, when the device is unavailable or the grid cannot run
/// there (rotated, or wider than one dispatch reaches).
bool RunCellKernel(const ComputeKernel& program, std::span<const std::byte> kernel,
                   Raster<float>& grid, const Vec2d& origin);

}  // namespace terraingen
