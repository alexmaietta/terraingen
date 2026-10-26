// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/cell_loops.h"

#include "raster/mapped_bounds.h"
#include "terraingen/util/bits.h"

#include <cmath>

namespace terraingen {

namespace {

/// `value` clamped to [0, `limit`].
std::size_t ClampTo(double value, std::size_t limit) {
    return ClampIndex(value, limit + 1);
}

}  // namespace

PixelWindow PixelWindowIn(const RasterGrid& grid, const Bounds2d& box) {
    const Bounds2d pixels =
        MappedBounds(box, [&](double x, double y) { return grid.transform.WorldToPixel(x, y); });
    return {ClampTo(std::floor(pixels.minX), grid.width),
            ClampTo(std::ceil(pixels.maxX), grid.width),
            ClampTo(std::floor(pixels.minY), grid.height),
            ClampTo(std::ceil(pixels.maxY), grid.height)};
}

RasterGrid Subgrid(const RasterGrid& grid, const PixelWindow& cells) {
    RasterGrid result{cells.Empty() ? 0 : cells.colEnd - cells.colBegin,
                      cells.Empty() ? 0 : cells.rowEnd - cells.rowBegin, grid.transform};
    if (cells.colBegin != 0 || cells.rowBegin != 0) {
        const Vec2d corner = grid.transform.PixelToWorld(static_cast<double>(cells.colBegin),
                                                         static_cast<double>(cells.rowBegin));
        result.transform.originX = corner.x;
        result.transform.originY = corner.y;
    }
    return result;
}

std::vector<PixelWindow> PixelWindowsIn(const RasterGrid& grid, const Bounds2d& box,
                                        const Surface& surface) {
    const std::vector<double> shifts =
        surface.IsBody() ? std::vector<double>{-360.0, 0.0, 360.0} : std::vector<double>{0.0};
    std::vector<PixelWindow> windows;
    for (const double shift : shifts) {
        const PixelWindow window =
            PixelWindowIn(grid, {box.minX + shift, box.minY, box.maxX + shift, box.maxY});
        if (!window.Empty()) {
            windows.push_back(window);
        }
    }
    return windows;
}

}  // namespace terraingen
