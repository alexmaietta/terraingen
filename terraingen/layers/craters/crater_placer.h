/// @file
/// @brief `CraterPlacer`, a layer that lists the craters it applies, and `PlacedCraters`.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"

#include <memory>
#include <span>
#include <vector>

namespace terraingen {

/// Craters and their attributes, in step.
struct CraterFeatures {
    CraterList craters;
    std::vector<CraterAttributes> attributes;

    /// Appends `other`'s craters and attributes.
    void Append(const CraterFeatures& other);
};

/// A layer that places craters. Craters are a pure function of position, so the layer lists them
/// on request rather than recording them while it is applied.
class CraterPlacer {
  public:
    virtual ~CraterPlacer() = default;

    /// The craters the layer applies over `grid`, as its `Apply` there sees them.
    /// @param spacing The sample spacing in metres, raised to the grid's as `Apply` raises it.
    virtual CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const = 0;
};

/// The craters `layers` apply over `grid`, layer by layer in order: those of every
/// `CraterPlacer` among them.
/// @param spacing The sample spacing in metres.
CraterFeatures PlacedCraters(std::span<const std::shared_ptr<const Layer>> layers,
                             const RasterGrid& grid, double spacing);

}  // namespace terraingen
