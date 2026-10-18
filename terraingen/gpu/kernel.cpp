#include "terraingen/gpu/kernel.h"

#include "terraingen/gpu/cuda.h"
#include "terraingen/gpu/metal.h"
#include "terraingen/gpu/vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terraingen {

namespace {

/// Most cells one dispatch covers, keeping each buffer under the smallest storage buffer range
/// Vulkan allows (128 MiB).
constexpr std::size_t kMaxStripCells = std::size_t{1} << 24;

/// Cells per workgroup side in kernels.
constexpr std::size_t kGroupSide = 16;

/// Most workgroups Vulkan guarantees in one dimension of a dispatch; CUDA and Metal allow more.
constexpr std::size_t kMaxGroups = 65535;

/// The push constants of kernels (`terraingen/gpu/kernel.slang`).
struct KernelParams {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::uint32_t noData = 0;  ///< 0 for none, 1 for NaN, 2 for `noDataValue`.
    float noDataValue = 0.0f;
    std::uint32_t firstRow = 0;  ///< The strip's first row in the grid.
};

}  // namespace

bool RunKernel(Backend backend, const KernelProgram& program, std::span<const std::byte> kernel,
               Raster<float>& grid, const Vec2d& origin, std::span<const KernelBuffer> extra) {
    const GeoTransform& transform = grid.Transform();
    const std::size_t width = grid.Width();
    if (transform.rowRotation != 0.0 || transform.colRotation != 0.0 || width == 0 ||
        grid.Height() == 0 || (width + kGroupSide - 1) / kGroupSide > kMaxGroups) {
        return false;
    }
    VulkanCompute* vulkan = nullptr;
    CudaCompute* cuda = nullptr;
    MetalCompute* metal = nullptr;
    switch (backend) {
    case Backend::Vulkan:
        vulkan = program.spirv.empty() ? nullptr : VulkanCompute::Get();
        break;
    case Backend::Cuda:
        cuda = program.ptx.empty() ? nullptr : CudaCompute::Get();
        break;
    case Backend::Metal:
        metal = program.metal.empty() ? nullptr : MetalCompute::Get();
        break;
    default:
        break;
    }
    if (vulkan == nullptr && cuda == nullptr && metal == nullptr) {
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
    std::vector<std::vector<std::byte>> extraCopies;
    for (const KernelBuffer& buffer : extra) {
        extraCopies.emplace_back(buffer.bytes.begin(), buffer.bytes.end());
    }
    KernelParams push;
    push.width = static_cast<std::uint32_t>(width);
    if (const auto& noData = grid.NoData()) {
        push.noData = std::isnan(*noData) ? 1u : 2u;
        push.noDataValue = *noData;
    }
    const std::size_t stripRows =
        std::clamp<std::size_t>(kMaxStripCells / width, 1, kMaxGroups * kGroupSide);
    const std::array<std::uint32_t, 3> groupSize{kGroupSide, kGroupSide, 1u};
    std::span<float> values(grid.Values());
    for (std::size_t row0 = 0; row0 < ys.size(); row0 += stripRows) {
        const std::size_t rows = std::min(stripRows, ys.size() - row0);
        push.rows = static_cast<std::uint32_t>(rows);
        push.firstRow = static_cast<std::uint32_t>(row0);
        std::vector<std::span<std::byte>> buffers{
            std::as_writable_bytes(values.subspan(row0 * width, rows * width)),
            std::as_writable_bytes(std::span(xs)),
            std::as_writable_bytes(std::span(ys).subspan(row0, rows)), std::span(block)};
        for (std::vector<std::byte>& copy : extraCopies) {
            buffers.push_back(std::span(copy));
        }
        const std::span<const std::byte> pushBytes = std::as_bytes(std::span(&push, 1));
        const std::array<std::uint32_t, 3> groups{
            static_cast<std::uint32_t>((width + kGroupSide - 1) / kGroupSide),
            static_cast<std::uint32_t>((rows + kGroupSide - 1) / kGroupSide), 1u};
        if (vulkan != nullptr) {
            vulkan->Run({program.spirv.data(), program.spirv.size()}, buffers, pushBytes, groups);
        } else if (cuda != nullptr) {
            std::vector<CudaBuffer> cudaBuffers{{buffers[0], rows * width},
                                                {buffers[1], width},
                                                {buffers[2], rows},
                                                {buffers[3], 1}};
            for (std::size_t n = 0; n < extra.size(); ++n) {
                cudaBuffers.push_back({buffers[4 + n], extra[n].count});
            }
            cuda->Run(program.ptx, cudaBuffers, pushBytes, groups, groupSize);
        } else {
            metal->Run(program.metal, buffers, pushBytes, groups, groupSize);
        }
    }
    return true;
}

}  // namespace terraingen
