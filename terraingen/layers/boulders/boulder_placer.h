/// @file
/// @brief `BoulderPlacer`, a layer that lists the boulders it places.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/ground.h"
#include "terraingen/raster/grid.h"

#include <vector>

namespace terraingen {

/// Boulders, and those of them placed as their own 3D models.
struct BoulderFeatures {
    std::vector<Boulder> boulders;     ///< Every boulder, in the order placed.
    std::vector<BoulderModel> models;  ///< The boulders placed as their own 3D models.

    /// Appends `other`'s boulders and models.
    void Append(const BoulderFeatures& other) {
        boulders.insert(boulders.end(), other.boulders.begin(), other.boulders.end());
        models.insert(models.end(), other.models.begin(), other.models.end());
    }
};

/// A layer that places boulders. Boulders are a pure function of position, so the layer lists
/// them on request rather than recording them while it is applied.
class BoulderPlacer {
  public:
    virtual ~BoulderPlacer() = default;

    /// The boulders the layer places over `grid`, each read on
    /// `ground` (null leaves the ground unknown, and models unplaced).
    /// @param spacing The sample spacing in metres models are marked at, finer than the grid's or
    /// not (`ListedSpacing`); 0 for the grid's. Every boulder of the layer's diameter range is
    /// listed, however small, since those too small for the heights stand as models or in lists.
    virtual BoulderFeatures BouldersOver(const RasterGrid& grid, double spacing,
                                         const Ground* ground) const = 0;
};

}  // namespace terraingen
