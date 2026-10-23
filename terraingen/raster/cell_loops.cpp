#include "terraingen/raster/cell_loops.h"

#include <cmath>
#include <limits>

namespace terraingen {

namespace {

std::size_t ClampTo(double value, std::size_t limit) {
    return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
}

}  // namespace

PixelWindow PixelWindowIn(const RasterGrid& grid, const Bounds2d& box) {
    const GeoTransform& transform = grid.transform;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double colMin = kInf;
    double colMax = -kInf;
    double rowMin = kInf;
    double rowMax = -kInf;
    for (const double wx : {box.minX, box.maxX}) {
        for (const double wy : {box.minY, box.maxY}) {
            const Vec2d pixel = transform.WorldToPixel(wx, wy);
            colMin = std::fmin(colMin, pixel.x);
            colMax = std::fmax(colMax, pixel.x);
            rowMin = std::fmin(rowMin, pixel.y);
            rowMax = std::fmax(rowMax, pixel.y);
        }
    }
    return {ClampTo(std::floor(colMin), grid.width), ClampTo(std::ceil(colMax), grid.width),
            ClampTo(std::floor(rowMin), grid.height), ClampTo(std::ceil(rowMax), grid.height)};
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

std::vector<PixelWindow> PixelWindowsIn(const Raster<float>& raster, const Bounds2d& box,
                                        const Surface& surface) {
    return PixelWindowsIn(RasterGrid::FromRaster(raster), box, surface);
}

}  // namespace terraingen
