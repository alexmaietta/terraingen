/// @file
/// @brief The filter interface: transforms of the heights after every layer.
#pragma once

#include "terraingen/raster/raster.h"

#include <memory>
#include <span>

namespace terraingen {

class Layer;

/// @brief How a filter is evaluated.
struct FilterContext {
    double spacing = 0.0;  ///< Sample spacing in metres, as for `Layer::Apply`.
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    /// The layers the heights went through, for filters that read their features.
    std::span<const std::shared_ptr<const Layer>> layers{};
};

/// @brief A transform of the heights the source and layers produced: smoothing, reshaping, or a
/// filter of your own. Filters run after every layer, in order.
///
/// A filter must be deterministic and thread safe. Unlike a layer, it may read the heights around a
/// cell, so it runs on grids only, and a grid evaluated tile by tile needs `Margin` cells around
/// each tile to equal the whole.
class Filter {
  public:
    virtual ~Filter() = default;

    /// Transforms `heights` in place.
    virtual void Apply(Raster<float>& heights, const FilterContext& context) const = 0;

    /// How far from a cell the filter reads, in metres, for features up to `maxFeatureDiameter`
    /// metres across; 0 by default, for filters that read only the cell itself.
    virtual double Margin(double /*maxFeatureDiameter*/, double /*spacing*/) const {
        return 0.0;
    }
};

}  // namespace terraingen
