#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>

namespace terraingen {

/// A grid of cells without values: its size and its pixel-to-world transform. Terrains are
/// evaluated over grids, so a tile of a larger grid is a grid on the same lattice.
struct RasterGrid {
    std::size_t width = 0;
    std::size_t height = 0;
    GeoTransform transform{};

    /// The grid of `raster`.
    static RasterGrid Of(const Raster<float>& raster) {
        return {raster.Width(), raster.Height(), raster.Transform()};
    }

    /// The axis-aligned box in world coordinates that the grid covers.
    Bounds2d Bounds() const;

    /// Area of the grid in square world units.
    double Area() const;

    /// Side of a cell: the square root of a cell's area.
    double PixelSize() const;

    /// The area the grid covers on `surface`, in m^2: `Area` on a plane, and on a sphere of radius
    /// R the area of its longitude-latitude box, R^2 (lon2 - lon1) (sin lat2 - sin lat1), with
    /// latitudes clamped to [-90, 90] and at most a full turn of longitude.
    double SurfaceArea(const Surface& surface) const;

    /// The grid's sample spacing in metres on `surface`: `PixelSize` on a plane, and on a sphere
    /// the latitude step of a cell (its largest side) in metres.
    double Spacing(const Surface& surface) const;

    /// A raster on this grid filled with `fill`.
    Raster<float> MakeRaster(float fill = 0.0f) const;

    bool operator==(const RasterGrid&) const = default;
};

/// @brief The spacing a raster is sampled at: a requested spacing raised to the raster's own.
/// @param raster The raster.
/// @param spacing The requested spacing in metres; 0 for the raster's own.
/// @param surface What the raster lies on (`RasterGrid::Spacing`).
/// @return The larger of `spacing` and the raster's spacing on `surface`.
/// @throws std::domain_error if the raster's transform is not invertible.
double ResolvedSpacing(const Raster<float>& raster, double spacing, const Surface& surface);

}  // namespace terraingen
