/// @file
/// @brief The source interface: where a terrain's heights come from.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <limits>
#include <optional>

namespace terraingen {

/// Where a terrain's heights come from: a DEM, synthetic noise, or a source of your own. A source
/// only loads or generates heights; how its posts become heights at finer sample spacings is its
/// upsampler's job (`Upsampler`). Layers add relief to what the source gives, then filters
/// transform the result.
///
/// Heights are a function of position and sample spacing only, so points, tiles and grids at the
/// same spacing agree. Sources must be deterministic and thread safe: a terrain may be evaluated
/// tile by tile, in parallel.
class Source {
  public:
    virtual ~Source() = default;

    /// The grid the source covers, which a terrain is evaluated over unless another is given. A
    /// source without bounds, such as synthetic noise, gives a grid without cells whose
    /// transform places its posts, if it has any (`BoundedGrid`).
    virtual RasterGrid Grid() const = 0;

    /// A point near the source's data, in world units, that noise layers measure positions from,
    /// keeping single-precision noise accurate where coordinates are large; the origin by default.
    virtual Vec2d ReferencePoint() const {
        return {};
    }

    /// The projection outputs over the source's grid are written with; none by default.
    virtual GeoKeys Keys() const {
        return {};
    }

    /// Overwrites every cell of `heights`, placed by its transform, with the source's height
    /// there at a sample spacing of `spacing`, or with `heights`' no-data value where the source
    /// has none.
    /// @param spacing Sample spacing in the source's units (meters on a plane, degrees on a
    /// body): finer than the source's posts, they are refined to it.
    virtual void Fill(Raster<float>& heights, double spacing) const = 0;

    /// `Fill` at the grid's own spacing, its cell size (`RasterGrid::PixelSize`).
    void Fill(Raster<float>& heights) const {
        Fill(heights, RasterGrid::FromRaster(heights).PixelSize());
    }

    /// The height in meters at world point (`x`, `y`) at a sample spacing of `spacing` in the
    /// source's units, or NaN where the source has no data: what `Fill` gives a cell centered
    /// there at that spacing, to the same bits.
    virtual float HeightAt(double x, double y, double spacing) const = 0;

    /// The cell size of the data that gives the height at world point (`x`, `y`), before any
    /// upsampling, in CRS units (meters on a plane, degrees for a source in longitude and
    /// latitude, which `Surface::UnitLength` converts along a meridian): the scale below which the
    /// source resolves no features, so a 240 m DEM upsampled to 2 m still resolves only what 240 m
    /// cells do. Infinity where no measured data gives the height, as for synthetic noise, the
    /// default.
    virtual double ResolutionAt(double /*x*/, double /*y*/) const {
        return std::numeric_limits<double>::infinity();
    }
};

/// The grid `source` covers, or none for a source without bounds (a grid without cells).
inline std::optional<RasterGrid> BoundedGrid(const Source& source) {
    RasterGrid grid = source.Grid();
    if (grid.width == 0 || grid.height == 0) {
        return std::nullopt;
    }
    return grid;
}

}  // namespace terraingen
