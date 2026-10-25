// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The affine pixel-to-world transform of a georeferenced raster.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <stdexcept>

namespace terraingen {

/// Affine mapping from pixel to world coordinates, as GDAL's six-coefficient geotransform.
///
/// Pixel (0, 0) is the outer corner of the first pixel, so centers sit at half-integer
/// coordinates. World coordinates are in the CRS's units (meters when projected). North-up rasters
/// have a negative `pixelHeight` and no rotation. The default maps pixels to world one to one, as
/// GDAL does for ungeoreferenced rasters.
struct GeoTransform {
    double originX = 0.0;      ///< World x of pixel coordinate (0, 0).
    double pixelWidth = 1.0;   ///< World x change per column.
    double rowRotation = 0.0;  ///< World x change per row.
    double originY = 0.0;      ///< World y of pixel coordinate (0, 0).
    double colRotation = 0.0;  ///< World y change per column.
    double pixelHeight = 1.0;  ///< World y change per row; negative for north-up.

    /// A north-up transform with square pixels.
    /// @param originX World x of the raster's west edge.
    /// @param originY World y of the raster's north edge.
    /// @param pixelSize Pixel edge length in world units; positive.
    static GeoTransform NorthUp(double originX, double originY, double pixelSize) {
        return GeoTransform{originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    }

    /// A transform from coefficients in the order `GDALDataset::GetGeoTransform` gives them.
    static GeoTransform FromGdal(const std::array<double, 6>& c) {
        return GeoTransform{c[0], c[1], c[2], c[3], c[4], c[5]};
    }

    /// The coefficients in GDAL's order, as passed to `GDALDataset::SetGeoTransform`.
    std::array<double, 6> ToGdal() const {
        return {originX, pixelWidth, rowRotation, originY, colRotation, pixelHeight};
    }

    /// Whether rows or columns are rotated against the world axes.
    bool IsRotated() const {
        return rowRotation != 0.0 || colRotation != 0.0;
    }

    /// Maps pixel coordinates, 0 at the first pixel's outer edge, to world coordinates.
    Vec2d PixelToWorld(double col, double row) const {
        return {originX + col * pixelWidth + row * rowRotation,
                originY + col * colRotation + row * pixelHeight};
    }

    /// Maps world coordinates to pixel coordinates (column, row).
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

    /// Coefficientwise equality.
    bool operator==(const GeoTransform& other) const {
        return originX == other.originX && pixelWidth == other.pixelWidth &&
               rowRotation == other.rowRotation && originY == other.originY &&
               colRotation == other.colRotation && pixelHeight == other.pixelHeight;
    }
    bool operator!=(const GeoTransform& other) const {
        return !(*this == other);
    }
};

}  // namespace terraingen
