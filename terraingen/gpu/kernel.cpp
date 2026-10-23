#include "terraingen/gpu/kernel.h"

#include "terraingen/gpu/device.h"
#include "terraingen/util/arithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terraingen {

namespace {

/// Most bytes a strip's buffer of the grid's cells holds, under the smallest storage buffer range
/// Vulkan allows (128 MiB).
constexpr std::size_t kMaxStripBytes = std::size_t{1} << 26;

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

/// The grid in workgroups of `kGroupSide` cells, rounded up.
std::uint32_t Groups(std::size_t cells) {
    return static_cast<std::uint32_t>(CeilDiv(cells, kGroupSide));
}

/// Whether kernels can run over `grid`: not rotated, not empty, and no wider than one dispatch.
bool Dispatchable(const Raster<float>& grid) {
    const GeoTransform& transform = grid.Transform();
    return !transform.IsRotated() && grid.Width() > 0 && grid.Height() > 0 &&
           Groups(grid.Width()) <= kMaxGroups;
}

/// The x of each column's cell centers, measured from `origin`.
std::vector<float> ColumnOffsets(const Raster<float>& grid, const Vec2d& origin) {
    std::vector<float> xs(grid.Width());
    for (std::size_t col = 0; col < xs.size(); ++col) {
        xs[col] = static_cast<float>(grid.CellCenter(col, 0).x - origin.x);
    }
    return xs;
}

/// The y of each row's cell centers, measured from `origin`.
std::vector<float> RowOffsets(const Raster<float>& grid, const Vec2d& origin) {
    std::vector<float> ys(grid.Height());
    for (std::size_t row = 0; row < ys.size(); ++row) {
        ys[row] = static_cast<float>(grid.CellCenter(0, row).y - origin.y);
    }
    return ys;
}

/// The push constants shared by every strip of `grid`.
KernelParams GridParams(const Raster<float>& grid) {
    KernelParams params;
    params.width = static_cast<std::uint32_t>(grid.Width());
    if (const auto& noData = grid.NoData()) {
        params.noData = std::isnan(*noData) ? 1u : 2u;
        params.noDataValue = *noData;
    }
    return params;
}

/// The most bytes any buffer of `grid`'s cells holds per cell: its heights or one of `extra`.
std::size_t CellBytes(const Raster<float>& grid, Span<const KernelBuffer> extra) {
    std::size_t bytes = sizeof(float);
    for (const KernelBuffer& buffer : extra) {
        if (buffer.perCell != 0) {
            bytes = std::max(bytes, buffer.bytes.size() / grid.Size());
        }
    }
    return bytes;
}

/// A whole grid's kernel run, one strip of rows at a time.
struct StripRun {
    ComputeDevice& device;
    const KernelProgram& program;
    Span<const std::byte> kernel;
    Span<const KernelBuffer> extra;
    std::vector<float> xs;
    std::vector<float> ys;
    KernelParams params;

    /// Runs the kernel over `rows` rows from `row0`, updating those rows of `values` in place.
    void Run(Span<float> values, std::size_t row0, std::size_t rows) {
        const std::size_t width = xs.size();
        params.rows = static_cast<std::uint32_t>(rows);
        params.firstRow = static_cast<std::uint32_t>(row0);
        std::vector<DeviceBuffer> buffers{
            OutputBuffer(AsWritableBytes(values.subspan(row0 * width, rows * width)), rows * width),
            InputBuffer(AsBytes(Span(xs)), width),
            InputBuffer(AsBytes(Span(ys).subspan(row0, rows)), rows), InputBuffer(kernel, 1)};
        for (const KernelBuffer& buffer : extra) {
            if (buffer.perCell == 0) {
                buffers.push_back(InputBuffer(buffer.bytes, buffer.count));
                continue;
            }
            const std::size_t elements = buffer.perCell * width;
            const std::size_t bytes = buffer.bytes.size() / buffer.count * elements;
            buffers.push_back(
                InputBuffer(buffer.bytes.subspan(row0 * bytes, rows * bytes), rows * elements));
        }
        const Dispatch dispatch{{Groups(width), Groups(rows), 1u}, {kGroupSide, kGroupSide, 1u}};
        device.Run(program, buffers, AsBytes(Span(&params, 1)), dispatch);
    }
};

}  // namespace

bool RunKernel(Backend backend, const KernelProgram& program, Span<const std::byte> kernel,
               Raster<float>& grid, const Vec2d& origin, Span<const KernelBuffer> extra) {
    if (!Dispatchable(grid)) {
        return false;
    }
    ComputeDevice* device = GpuDevice(backend);
    if (device == nullptr || !device->CanRun(program)) {
        return false;
    }
    StripRun run{*device,
                 program,
                 kernel,
                 extra,
                 ColumnOffsets(grid, origin),
                 RowOffsets(grid, origin),
                 GridParams(grid)};
    const std::size_t height = grid.Height();
    const std::size_t stripRows = std::clamp<std::size_t>(
        kMaxStripBytes / (CellBytes(grid, extra) * grid.Width()), 1, kMaxGroups * kGroupSide);
    for (std::size_t row0 = 0; row0 < height; row0 += stripRows) {
        run.Run(grid.Values(), row0, std::min(stripRows, height - row0));
    }
    return true;
}

}  // namespace terraingen
