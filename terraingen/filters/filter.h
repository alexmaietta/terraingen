#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// A transform of the heights the source and layers produced: smoothing, reshaping, or a filter of
/// your own. Filters run after every layer, in order, and see the features the layers placed.
///
/// A filter must be deterministic and safe to call from several threads at once. Unlike a layer, it
/// may read the heights around a cell, so a filter evaluated tile by tile needs a margin of cells
/// around each tile to equal the whole. `spacing` is as for `Layer::Apply`.
class Filter {
  public:
    virtual ~Filter() = default;

    /// Transforms `heights` in place.
    virtual void Apply(Raster<float>& heights, double spacing,
                       const TerrainFeatures& features) const = 0;
};

}  // namespace terraingen
