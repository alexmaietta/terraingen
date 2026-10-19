/// @file
/// @brief The source interface: where a terrain's heights come from.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <limits>

namespace terraingen {

/// Where a terrain's heights come from: a flat datum, a DEM, or a source of your own. Layers
/// add relief to what the source gives, then filters transform the result.
///
/// Sources must be deterministic and thread safe: a terrain may be evaluated tile by tile, in
/// parallel, and the tiles must agree with the whole.
class Source {
  public:
    virtual ~Source() = default;

    /// The grid the source covers, which a terrain is evaluated over unless another is given.
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
    /// there, or with `heights`' no-data value where the source has none.
    virtual void Fill(Raster<float>& heights) const = 0;

    /// The height in metres at world point (`x`, `y`), or NaN where the source has no data: what
    /// `Fill` gives a cell centred there, to the same bits.
    virtual float HeightAt(double x, double y) const = 0;

    /// The cell size of the data that gives the height at world point (`x`, `y`), before any
    /// resampling, in CRS units (metres on a plane, degrees for a source in longitude and
    /// latitude, which `Surface::UnitLength` converts along a meridian): the scale below which the
    /// source resolves no features, so a 240 m DEM resampled to 2 m still resolves only what 240 m
    /// cells do. Infinity where no measured data gives the height, as for synthetic and flat
    /// sources, the default.
    virtual double ResolutionAt(double /*x*/, double /*y*/) const {
        return std::numeric_limits<double>::infinity();
    }
};

}  // namespace terraingen
