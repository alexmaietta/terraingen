/// @file
/// @brief The affine pixel-to-world transform of a georeferenced raster.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <stdexcept>

namespace terraingen {

/// @brief Affine mapping from pixel to world coordinates, as GDAL's six-coefficient geotransform.
///
/// Pixel (0, 0) is the outer corner of the first pixel, so centres sit at half-integer
/// coordinates. World coordinates are in the CRS's units (metres when projected). North-up rasters
/// have a negative `pixelHeight` and no rotation. The default maps pixels to world one to one, as
/// GDAL does for ungeoreferenced rasters.
struct GeoTransform {
    double originX = 0.0;      ///< World x of pixel coordinate (0, 0).
    double pixelWidth = 1.0;   ///< World x change per column.
    double rowRotation = 0.0;  ///< World x change per row.
    double originY = 0.0;      ///< World y of pixel coordinate (0, 0).
    double colRotation = 0.0;  ///< World y change per column.
    double pixelHeight = 1.0;  ///< World y change per row; negative for north-up.

    /// @brief A north-up transform with square pixels.
    /// @param originX World x of the raster's west edge.
    /// @param originY World y of the raster's north edge.
    /// @param pixelSize Pixel edge length in world units; positive.
    /// @return The transform.
    static GeoTransform NorthUp(double originX, double originY, double pixelSize) {
        return GeoTransform{originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    }

    /// @brief A transform from GDAL's coefficient order.
    /// @param c The coefficients as `GDALDataset::GetGeoTransform` gives them.
    /// @return The transform.
    static GeoTransform FromGdal(const std::array<double, 6>& c) {
        return GeoTransform{c[0], c[1], c[2], c[3], c[4], c[5]};
    }

    /// @brief The coefficients in GDAL's order.
    /// @return The coefficients as passed to `GDALDataset::SetGeoTransform`.
    std::array<double, 6> ToGdal() const {
        return {originX, pixelWidth, rowRotation, originY, colRotation, pixelHeight};
    }

    /// @brief Maps pixel coordinates to world coordinates.
    /// @param col Column coordinate, 0 at the first pixel's outer edge.
    /// @param row Row coordinate, 0 at the first pixel's outer edge.
    /// @return The world position.
    Vec2d PixelToWorld(double col, double row) const {
        return {originX + col * pixelWidth + row * rowRotation,
                originY + col * colRotation + row * pixelHeight};
    }

    /// @brief Maps world coordinates to pixel coordinates.
    /// @param x World x.
    /// @param y World y.
    /// @return The pixel position as (column, row).
    /// @throws std::domain_error if the transform is not invertible.
    Vec2d WorldToPixel(double x, double y) const {
        const double det = pixelWidth * pixelHeight - rowRotation * colRotation;
        if (det == 0.0) {
            throw std::domain_error("GeoTransform is not invertible");
        }
        const double dx = x - originX;
        const double dy = y - originY;
        return {(dx * pixelHeight - dy * rowRotation) / det,
                (dy * pixelWidth - dx * colRotation) / det};
    }

    bool operator==(const GeoTransform&) const = default;
};

}  // namespace terraingen
