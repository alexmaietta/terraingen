/// @file
/// @brief The craters and boulders a terrain's built-in layers place over a grid.
///
/// Features are a pure function of position, so they are listed on request rather than recorded
/// while evaluating. These gather them from `CraterLayer`, `SecondaryCraterLayer` and
/// `BoulderLayer`, also inside a `RegionLayer` (only the features it `Holds`); a layer of your
/// own lists its features its own way.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <vector>

namespace terraingen {

/// @brief Craters and their attributes, in step.
struct CraterFeatures {
    CraterList craters;
    std::vector<CraterAttributes> attributes;
};

/// @brief Boulders, and those of them placed as their own 3D models.
struct BoulderFeatures {
    std::vector<Boulder> boulders;
    std::vector<BoulderModel> models;
};

/// The craters `layers` apply over `grid`, layer by layer in order: those reaching the grid.
/// @param spacing The sample spacing in metres; raised to the grid's, as `Evaluate` does.
CraterFeatures PlacedCraters(const std::vector<std::shared_ptr<const Layer>>& layers,
                             const RasterGrid& grid, double spacing, const Surface& surface);

/// The craters a terrain's layers apply over `grid`, as `PlacedCraters`.
CraterFeatures PlacedCraters(const Terrain& terrain, const RasterGrid& grid, double spacing = 0.0);

/// The boulders a terrain's layers place over `grid`, each on the ground beneath its layer, and
/// the boulder models among them.
/// @param spacing The sample spacing in metres; raised to the grid's, as `Evaluate` does.
BoulderFeatures PlacedBoulders(const Terrain& terrain, const RasterGrid& grid,
                               double spacing = 0.0);

}  // namespace terraingen
