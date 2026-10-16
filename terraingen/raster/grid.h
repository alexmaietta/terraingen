/// @file
/// @brief Raster grids without values, and resampled grids and spacings derived from them.
#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>

namespace terraingen {

/// @brief A grid of cells without values: its size and pixel-to-world transform.
///
/// Terrains are evaluated over grids, so a tile of a larger grid is a grid on the same lattice.
struct RasterGrid {
    std::size_t width = 0;     ///< Columns.
    std::size_t height = 0;    ///< Rows.
    GeoTransform transform{};  ///< Pixel-to-world transform.

    /// @brief The grid of a raster.
    /// @param raster The raster.
    /// @return Its size and transform.
    static RasterGrid Of(const Raster<float>& raster) {
        return {raster.Width(), raster.Height(), raster.Transform()};
    }

    /// @brief The axis-aligned box the grid covers.
    /// @return The box in world coordinates.
    Bounds2d Bounds() const;

    /// @brief The grid's area.
    /// @return The area in square world units.
    double Area() const;

    /// @brief The side of a cell: the square root of a cell's area.
    /// @return The side in world units.
    double PixelSize() const;

    /// @brief The area the grid covers on a surface.
    ///
    /// On a sphere of radius R this is R^2 (lon2 - lon1) (sin lat2 - sin lat1) over the grid's
    /// longitude-latitude box, with latitudes clamped to [-90, 90] and at most a full turn.
    /// @param surface What the grid lies on.
    /// @return The area in m^2: `Area` on a plane.
    double SurfaceArea(const Surface& surface) const;

    /// @brief The grid's sample spacing on a surface.
    /// @param surface What the grid lies on.
    /// @return The spacing in metres: `PixelSize` on a plane, on a sphere the latitude step of a
    /// cell (its largest side).
    double Spacing(const Surface& surface) const;

    /// @brief A raster on this grid.
    /// @param fill The value of every cell.
    /// @return The raster, with this grid's size and transform and no no-data value.
    Raster<float> MakeRaster(float fill = 0.0f) const;

    bool operator==(const RasterGrid&) const = default;
};

/// @brief A grid with square cells over the same extent, origin and axis directions.
/// @param grid An unrotated grid.
/// @param pixelSize The new cell side in world units; positive and finite.
/// @return A grid of round(width * |pixelWidth| / `pixelSize`) x round(height * |pixelHeight| /
/// `pixelSize`) cells, at least one each.
/// @throws std::invalid_argument for a rotated grid, a `pixelSize` that is not positive and finite,
/// or a result wider or taller than 2^31 cells.
RasterGrid ResampledGrid(const RasterGrid& grid, double pixelSize);

/// @brief The spacing a raster is sampled at: a requested spacing raised to the raster's own.
/// @param raster The raster.
/// @param spacing The requested spacing in metres; 0 for the raster's own.
/// @param surface What the raster lies on (`RasterGrid::Spacing`).
/// @return The larger of `spacing` and the raster's spacing on `surface`.
/// @throws std::domain_error if the raster's transform is not invertible.
double ResolvedSpacing(const Raster<float>& raster, double spacing, const Surface& surface);

}  // namespace terraingen
