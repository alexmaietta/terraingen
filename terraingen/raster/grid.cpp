#include "terraingen/raster/grid.h"

#include <cmath>
#include <limits>

namespace terraingen {

Bounds2d RasterGrid::Bounds() const {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    Bounds2d bounds{kInf, kInf, -kInf, -kInf};
    for (const double col : {0.0, static_cast<double>(width)}) {
        for (const double row : {0.0, static_cast<double>(height)}) {
            const Vec2d corner = transform.PixelToWorld(col, row);
            bounds.minX = std::fmin(bounds.minX, corner.x);
            bounds.minY = std::fmin(bounds.minY, corner.y);
            bounds.maxX = std::fmax(bounds.maxX, corner.x);
            bounds.maxY = std::fmax(bounds.maxY, corner.y);
        }
    }
    return bounds;
}

double RasterGrid::Area() const {
    return static_cast<double>(width) * static_cast<double>(height) * PixelSize() * PixelSize();
}

double RasterGrid::PixelSize() const {
    return std::sqrt(std::fabs(transform.pixelWidth * transform.pixelHeight -
                               transform.rowRotation * transform.colRotation));
}

Raster<float> RasterGrid::MakeRaster(float fill) const {
    Raster<float> raster(width, height, fill);
    raster.SetTransform(transform);
    return raster;
}

double RasterGrid::Spacing(const Surface& surface) const {
    if (!surface.IsSphere()) {
        return PixelSize();
    }
    return std::hypot(transform.rowRotation, transform.pixelHeight) * surface.UnitLength();
}

}  // namespace terraingen
