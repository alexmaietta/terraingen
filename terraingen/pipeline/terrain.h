#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <vector>

namespace terraingen {

/// A terrain as a composition: heights from `source`, plus each of `layers` in order, then each of
/// `filters` in order. Every part is a pure function of where it is evaluated, so a terrain can be
/// evaluated over any grid on its source's lattice, such as a tile, at any sample spacing.
struct Terrain {
    std::shared_ptr<const Source> source;
    std::vector<std::shared_ptr<const Layer>> layers;
    std::vector<std::shared_ptr<const Filter>> filters;
};

/// Heights over a grid and the features the layers placed there.
struct EvaluatedTerrain {
    Raster<float> heights;
    TerrainFeatures features;
};

/// Evaluates `terrain` over `grid` at a sample spacing of `spacing` world units; 0 means the grid's
/// cell size, and a smaller value is raised to it.
/// @throws std::invalid_argument if `terrain` has no source or holds a null layer or filter, or
/// `spacing` is negative or not finite; and whatever the parts throw.
EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing = 0.0);

/// Evaluates `terrain` over its source's grid.
EvaluatedTerrain Evaluate(const Terrain& terrain);

}  // namespace terraingen
