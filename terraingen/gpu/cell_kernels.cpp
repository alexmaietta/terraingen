#include "terraingen/gpu/cell_kernels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terraingen {

namespace {

/// Most cells one dispatch covers, keeping each buffer under the smallest storage buffer range
/// Vulkan allows (128 MiB).
constexpr std::size_t kMaxStripCells = std::size_t{1} << 24;

/// Cells per workgroup side in cell kernels.
constexpr std::size_t kGroupSide = 16;

/// Most workgroups Vulkan guarantees in one dimension of a dispatch.
constexpr std::size_t kMaxGroups = 65535;

/// The push constants of cell kernels (`cmake/CellKernel.slang.in`).
struct CellKernelParams {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::uint32_t noData = 0;  ///< 0 for none, 1 for NaN, 2 for `noDataValue`.
    float noDataValue = 0.0f;
};

}  // namespace

bool RunCellKernel(const ComputeKernel& program, std::span<const std::byte> kernel,
                   Raster<float>& grid, const Vec2d& origin) {
    const GeoTransform& transform = grid.Transform();
    const std::size_t width = grid.Width();
    if (transform.rowRotation != 0.0 || transform.colRotation != 0.0 || width == 0 ||
        grid.Height() == 0 || (width + kGroupSide - 1) / kGroupSide > kMaxGroups) {
        return false;
    }
    VulkanCompute* compute = VulkanCompute::Get();
    if (compute == nullptr) {
        return false;
    }
    std::vector<float> xs(width);
    for (std::size_t col = 0; col < width; ++col) {
        xs[col] = static_cast<float>(grid.CellCenter(col, 0).x - origin.x);
    }
    std::vector<float> ys(grid.Height());
    for (std::size_t row = 0; row < ys.size(); ++row) {
        ys[row] = static_cast<float>(grid.CellCenter(0, row).y - origin.y);
    }
    std::vector<std::byte> block(kernel.begin(), kernel.end());
    CellKernelParams push;
    push.width = static_cast<std::uint32_t>(width);
    if (const auto& noData = grid.NoData()) {
        push.noData = std::isnan(*noData) ? 1u : 2u;
        push.noDataValue = *noData;
    }
    const std::size_t stripRows =
        std::clamp<std::size_t>(kMaxStripCells / width, 1, kMaxGroups * kGroupSide);
    std::span<float> values(grid.Values());
    for (std::size_t row0 = 0; row0 < ys.size(); row0 += stripRows) {
        const std::size_t rows = std::min(stripRows, ys.size() - row0);
        push.rows = static_cast<std::uint32_t>(rows);
        compute->Run(program,
                     {std::as_writable_bytes(values.subspan(row0 * width, rows * width)),
                      std::as_writable_bytes(std::span(xs)),
                      std::as_writable_bytes(std::span(ys).subspan(row0, rows)), std::span(block)},
                     std::as_bytes(std::span(&push, 1)),
                     {static_cast<std::uint32_t>((width + kGroupSide - 1) / kGroupSide),
                      static_cast<std::uint32_t>((rows + kGroupSide - 1) / kGroupSide), 1u});
    }
    return true;
}

}  // namespace terraingen
