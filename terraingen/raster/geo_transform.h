#pragma once

#include <array>
#include <stdexcept>

namespace terraingen {

/// A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

/// Affine mapping from pixel coordinates to world coordinates, matching GDAL's six-coefficient
/// geotransform.
///
/// Pixel coordinates are (column, row) with (0, 0) at the outer corner of the first pixel, so pixel
/// centres sit at half-integer coordinates. World coordinates are in the units of the raster's CRS
/// (metres for projected CRSs). For north-up rasters `pixelHeight` is negative and both rotation
/// terms are zero.
///
/// The default is GDAL's default for ungeoreferenced rasters, which maps pixels to world one to
/// one.
struct GeoTransform {
    double originX = 0.0;      ///< World x of pixel coordinate (0, 0).
    double pixelWidth = 1.0;   ///< World x change per column.
    double rowRotation = 0.0;  ///< World x change per row.
    double originY = 0.0;      ///< World y of pixel coordinate (0, 0).
    double colRotation = 0.0;  ///< World y change per column.
    double pixelHeight = 1.0;  ///< World y change per row; negative for north-up.

    /// Builds a north-up transform with square pixels.
    /// @param pixelSize Pixel edge length in world units; must be positive.
    static GeoTransform NorthUp(double originX, double originY, double pixelSize) {
        return GeoTransform{originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    }

    /// Builds a transform from GDAL's coefficient order.
    static GeoTransform FromGdal(const std::array<double, 6>& c) {
        return GeoTransform{c[0], c[1], c[2], c[3], c[4], c[5]};
    }

    /// Returns the coefficients in GDAL's order, as passed to `GDALDataset::SetGeoTransform`.
    std::array<double, 6> ToGdal() const {
        return {originX, pixelWidth, rowRotation, originY, colRotation, pixelHeight};
    }

    /// Maps pixel coordinates to world coordinates.
    Vec2d PixelToWorld(double col, double row) const {
        return {originX + col * pixelWidth + row * rowRotation,
                originY + col * colRotation + row * pixelHeight};
    }

    /// Maps world coordinates to pixel coordinates.
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
