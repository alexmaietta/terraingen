/// @file
/// @brief The craters and boulders a terrain's layers place over a grid.
#pragma once

#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"

namespace terraingen {

/// The craters a terrain's layers apply over `grid`: `PlacedCraters` of its layers.
/// @param spacing The sample spacing in metres, finer than the grid's or not; 0 for the grid's.
CraterFeatures PlacedCraters(const Terrain& terrain, const RasterGrid& grid, double spacing = 0.0);

/// The boulders a terrain's `BoulderPlacer` layers place over `grid`, each read on the ground
/// beneath its layer, and the boulder models among them.
/// @param spacing The sample spacing in metres, finer than the grid's or not; 0 for the grid's.
BoulderFeatures PlacedBoulders(const Terrain& terrain, const RasterGrid& grid,
                               double spacing = 0.0);

}  // namespace terraingen
