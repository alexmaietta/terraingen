#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

namespace terraingen {

/// Where a terrain's heights come from: a flat datum, a DEM, or a source of your own. Layers add
/// relief to what the source gives, then filters transform the result.
///
/// Sources must be deterministic and safe to call from several threads at once: a terrain may be
/// evaluated tile by tile, in parallel, and the tiles must agree with the whole.
class Source {
  public:
    virtual ~Source() = default;

    /// The grid the source covers, which a terrain is evaluated over unless another is given.
    virtual RasterGrid Grid() const = 0;

    /// A point near the source's data that noise layers measure positions from, so that
    /// single-precision noise stays accurate in projections whose coordinates are large.
    virtual Vec2d ReferencePoint() const {
        return {};
    }

    /// Sets every cell of `heights`, whose transform places the cells, to the source's height
    /// there, or to `heights`' no-data value where the source has none.
    virtual void Fill(Raster<float>& heights) const = 0;

    /// The height at (`x`, `y`): what `Fill` gives a cell centred there, to the same bits, or NaN
    /// where the source has no data.
    virtual float HeightAt(double x, double y) const = 0;
};

}  // namespace terraingen
