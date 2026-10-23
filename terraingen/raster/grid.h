/// @file
/// @brief Raster grids without values, and resampled grids and spacings derived from them.
#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>

namespace terraingen {

/// A grid of cells without values: its size and pixel-to-world transform.
///
/// Terrains are evaluated over grids, so a tile of a larger grid is a grid on the same lattice.
struct RasterGrid {
    std::size_t width = 0;     ///< Columns.
    std::size_t height = 0;    ///< Rows.
    GeoTransform transform{};  ///< Pixel-to-world transform.

    /// The size and transform of a raster.
    static RasterGrid FromRaster(const Raster<float>& raster) {
        return {raster.Width(), raster.Height(), raster.Transform()};
    }

    /// The axis-aligned box the grid covers, in world coordinates.
    Bounds2d Bounds() const;

    /// The grid's area in square world units.
    double Area() const;

    /// The side of a cell in world units: the square root of a cell's area.
    double PixelSize() const;

    /// The area in m^2 the grid covers on a surface: `Area` on a plane.
    ///
    /// On a sphere of radius R this is R^2 (lon2 - lon1) (sin lat2 - sin lat1) over the grid's
    /// longitude-latitude box, with latitudes clamped to [-90, 90] and at most a full turn; on an
    /// ellipsoid the area element is integrated over the same box.
    double SurfaceArea(const Surface& surface) const;

    /// The grid's sample spacing in meters on a surface: `PixelSize` on a plane, on a sphere the
    /// latitude step of a cell (its largest side).
    double Spacing(const Surface& surface) const;

    /// A raster with this grid's size and transform, every cell `fill`, and no no-data value.
    Raster<float> MakeRaster(float fill = 0.0f) const;

    /// Memberwise equality.
    bool operator==(const RasterGrid& other) const {
        return width == other.width && height == other.height && transform == other.transform;
    }
    bool operator!=(const RasterGrid& other) const {
        return !(*this == other);
    }
};

/// A grid with square cells over the same extent, origin and axis directions as an unrotated
/// `grid`: round(width * |pixelWidth| / `pixelSize`) x round(height * |pixelHeight| /
/// `pixelSize`) cells, at least one each.
/// @param pixelSize The new cell side in world units; positive and finite.
/// @throws std::invalid_argument for a rotated grid, a `pixelSize` that is not positive and finite,
/// or a result wider or taller than 2^31 cells.
RasterGrid ResampledGrid(const RasterGrid& grid, double pixelSize);

/// The spacing a raster is sampled at: the larger of `spacing` and the raster's own spacing on
/// `surface` (`RasterGrid::Spacing`).
/// @param spacing The requested spacing in meters; 0 for the raster's own.
/// @throws std::domain_error if the raster's transform is not invertible.
double ResolvedSpacing(const Raster<float>& raster, double spacing, const Surface& surface);

/// The spacing a grid is evaluated at: the larger of `spacing` and the grid's own spacing on
/// `surface` (`RasterGrid::Spacing`).
/// @param spacing The requested spacing in meters; 0 for the grid's own.
double ResolvedSpacing(const RasterGrid& grid, double spacing, const Surface& surface);

/// The spacing features are listed at over a grid: `spacing` when given, finer than the grid's
/// own or not, else the grid's own spacing on `surface`.
/// @param spacing The requested spacing in meters; 0 for the grid's own.
double ListedSpacing(const RasterGrid& grid, double spacing, const Surface& surface);

}  // namespace terraingen
