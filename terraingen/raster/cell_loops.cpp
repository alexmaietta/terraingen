/// @file
/// @brief Pixel windows of a raster around boxes on a plane or sphere.
#include "terraingen/raster/cell_loops.h"

#include <cmath>
#include <limits>

namespace terraingen {

namespace {

std::size_t ClampTo(double value, std::size_t limit) {
    return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
}

}  // namespace

PixelWindow WindowOf(const Raster<float>& raster, const Bounds2d& box) {
    const GeoTransform& transform = raster.Transform();
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
    return {ClampTo(std::floor(colMin), raster.Width()), ClampTo(std::ceil(colMax), raster.Width()),
            ClampTo(std::floor(rowMin), raster.Height()),
            ClampTo(std::ceil(rowMax), raster.Height())};
}

std::vector<PixelWindow> WindowsAround(const Raster<float>& raster, const Bounds2d& box,
                                       const Surface& surface) {
    std::vector<PixelWindow> windows;
    const auto add = [&](double shift) {
        const PixelWindow window =
            WindowOf(raster, {box.minX + shift, box.minY, box.maxX + shift, box.maxY});
        if (!window.Empty()) {
            windows.push_back(window);
        }
    };
    if (!surface.IsSphere()) {
        add(0.0);
        return windows;
    }
    for (const double shift : {-360.0, 0.0, 360.0}) {
        add(shift);
    }
    return windows;
}

}  // namespace terraingen
