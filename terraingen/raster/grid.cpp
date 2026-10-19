#include "terraingen/raster/grid.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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
    if (!surface.IsBody()) {
        return PixelSize();
    }
    return std::hypot(transform.rowRotation, transform.pixelHeight) * surface.UnitLength();
}

RasterGrid ResampledGrid(const RasterGrid& grid, double pixelSize) {
    RequirePositive(pixelSize, "Pixel size");
    const GeoTransform& source = grid.transform;
    if (source.IsRotated()) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
    const double scaleX = std::fabs(source.pixelWidth) / pixelSize;
    const double scaleY = std::fabs(source.pixelHeight) / pixelSize;
    const double width = std::max(1.0, std::round(static_cast<double>(grid.width) * scaleX));
    const double height = std::max(1.0, std::round(static_cast<double>(grid.height) * scaleY));
    if (width > 2147483647.0 || height > 2147483647.0) {
        throw std::invalid_argument("Resampled raster would be too large");
    }
    RasterGrid resampled{static_cast<std::size_t>(width), static_cast<std::size_t>(height), source};
    resampled.transform.pixelWidth = std::copysign(pixelSize, source.pixelWidth);
    resampled.transform.pixelHeight = std::copysign(pixelSize, source.pixelHeight);
    return resampled;
}

double ResolvedSpacing(const Raster<float>& raster, double spacing, const Surface& surface) {
    const RasterGrid grid = RasterGrid::FromRaster(raster);
    if (!(grid.PixelSize() > 0.0)) {
        throw std::domain_error("The raster's transform is not invertible");
    }
    return ResolvedSpacing(grid, spacing, surface);
}

double ResolvedSpacing(const RasterGrid& grid, double spacing, const Surface& surface) {
    return std::max(spacing, grid.Spacing(surface));
}

double RasterGrid::SurfaceArea(const Surface& surface) const {
    if (!surface.IsBody()) {
        return Area();
    }
    const Bounds2d box = Bounds();
    const double turn = std::fmin(box.maxX - box.minX, 360.0) * kDegree;
    const double south = std::fmax(box.minY, -90.0) * kDegree;
    const double north = std::fmin(box.maxY, 90.0) * kDegree;
    if (surface.IsSphere()) {
        const double radius = surface.Radius();
        return radius * radius * turn * std::fmax(0.0, std::sin(north) - std::sin(south));
    }
    if (!(north > south)) {
        return 0.0;
    }
    constexpr int kSteps = 64;
    const double west = box.minX * kDegree;
    double area = 0.0;
    for (int j = 0; j < kSteps; ++j) {
        const double lat = south + (north - south) * (j + 0.5) / kSteps;
        for (int i = 0; i < kSteps; ++i) {
            const double lon = west + turn * (i + 0.5) / kSteps;
            area += surface.AreaPerSteradian({lon * kRadian, lat * kRadian}) * std::cos(lat);
        }
    }
    return area * turn * (north - south) / (kSteps * kSteps);
}

}  // namespace terraingen
