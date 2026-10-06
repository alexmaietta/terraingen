#pragma once

#include "terraingen/layers/craters/population.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// What layers place besides heights, for the filters and outputs that follow them.
struct TerrainFeatures {
    CraterList craters;  ///< Every crater the layers applied, in the order they applied them.
};

/// Relief added on top of a source: noise, craters, or a layer of your own.
///
/// A layer must be deterministic and safe to call from several threads at once, and must place the
/// same features in a tile as in any larger grid containing it, so that a terrain evaluated tile by
/// tile equals the terrain evaluated whole. `spacing` is the sample spacing in world units, at
/// least the grid's cell size: features that cannot be resolved at it are left out.
class Layer {
  public:
    virtual ~Layer() = default;

    /// Adds the layer's relief to the cells of `heights` that hold data, and records the features
    /// it placed in `features`.
    virtual void Apply(Raster<float>& heights, double spacing, TerrainFeatures& features) const = 0;
};

}  // namespace terraingen
