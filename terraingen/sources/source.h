/// @file
/// @brief The source interface: where a terrain's heights come from.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

namespace terraingen {

/// @brief Where a terrain's heights come from: a flat datum, a DEM, or a source of your own. Layers
/// add relief to what the source gives, then filters transform the result.
///
/// Sources must be deterministic and thread safe: a terrain may be evaluated tile by tile, in
/// parallel, and the tiles must agree with the whole.
class Source {
  public:
    virtual ~Source() = default;

    /// @brief The grid the source covers, which a terrain is evaluated over unless another is
    /// given.
    /// @return The source's grid.
    virtual RasterGrid Grid() const = 0;

    /// @brief A point near the source's data that noise layers measure positions from, keeping
    /// single-precision noise accurate where coordinates are large.
    /// @return The reference point in world units; the origin by default.
    virtual Vec2d ReferencePoint() const {
        return {};
    }

    /// The projection outputs over the source's grid are written with; none by default.
    virtual GeoKeys Keys() const {
        return {};
    }

    /// @brief Sets every cell of `heights` to the source's height there, or to `heights`' no-data
    /// value where the source has none.
    /// @param heights Raster whose transform places the cells; overwritten.
    virtual void Fill(Raster<float>& heights) const = 0;

    /// @brief The height at a point: what `Fill` gives a cell centred there, to the same bits.
    /// @param x World x.
    /// @param y World y.
    /// @return Height in metres, or NaN where the source has no data.
    virtual float HeightAt(double x, double y) const = 0;
};

}  // namespace terraingen
