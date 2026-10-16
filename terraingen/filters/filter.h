/// @file
/// @brief The filter interface: transforms of the heights after every layer.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// @brief A transform of the heights the source and layers produced: smoothing, reshaping, or a
/// filter of your own. Filters run after every layer, in order, and see the features placed.
///
/// A filter must be deterministic and thread safe. Unlike a layer, it may read the heights around a
/// cell, so a filter evaluated tile by tile needs a margin of cells around each tile to equal the
/// whole.
class Filter {
  public:
    virtual ~Filter() = default;

    /// @brief Transforms `heights` in place.
    /// @param heights The heights to transform.
    /// @param spacing Sample spacing in metres, as for `Layer::Apply`.
    /// @param features The features the layers placed.
    virtual void Apply(Raster<float>& heights, double spacing,
                       const TerrainFeatures& features) const = 0;

    /// @brief `Apply` on up to `threads` threads, to the same bits; the default runs `Apply`.
    /// @param heights The heights to transform.
    /// @param spacing Sample spacing in metres.
    /// @param features The features the layers placed.
    /// @param threads Thread count; 0 for every hardware thread.
    virtual void ApplyParallel(Raster<float>& heights, double spacing,
                               const TerrainFeatures& features, unsigned /*threads*/) const {
        Apply(heights, spacing, features);
    }

    /// @brief How far from a cell the filter reads: grids evaluated with this margin, then cropped,
    /// equal the same cells of any larger grid.
    /// @param maxFeatureDiameter The largest feature, in metres.
    /// @param spacing Sample spacing in metres.
    /// @return The margin in world units.
    virtual double Margin(double maxFeatureDiameter, double spacing) const = 0;
};

}  // namespace terraingen
