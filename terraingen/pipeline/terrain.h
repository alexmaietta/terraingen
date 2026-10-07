#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/tiling.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace terraingen {

/// A terrain as a composition: heights from `source`, plus each of `layers` in order, then each of
/// `filters` in order. Every part is a pure function of where it is evaluated, so a terrain can be
/// evaluated over any grid on its source's lattice, such as a tile, at any sample spacing.
///
/// `surface` is what the terrain lies on; on a sphere, grids are in longitude and latitude and
/// spacings in metres (`RasterGrid::Spacing`). The layers and filters must be built for the same
/// surface.
struct Terrain {
    std::shared_ptr<const Source> source;
    std::vector<std::shared_ptr<const Layer>> layers;
    std::vector<std::shared_ptr<const Filter>> filters;
    Surface surface{};
};

/// How to evaluate a terrain over a grid.
struct EvaluationOptions {
    /// Sample spacing in metres; 0 means the grid's spacing on the terrain's surface, and a smaller
    /// value is raised to it.
    double spacing = 0.0;
    /// Threads to split each layer and filter over; 0 for every hardware thread. The result does
    /// not depend on it.
    unsigned threads = 0;
};

/// The ground beneath layer `layers` of `terrain`: its source and its first `layers` layers,
/// evaluated at single points with `ApplyAt`. `Evaluate` and `EvaluateAt` give each layer the
/// ground beneath it. `terrain` must outlive it.
class TerrainGround final : public Ground {
  public:
    /// @throws std::invalid_argument if `terrain` has no source or fewer than `layers` layers.
    TerrainGround(const Terrain& terrain, std::size_t layers);

    float HeightAt(double x, double y, double spacing) const override;

  private:
    const Terrain& terrain_;
    std::size_t layers_;
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

/// `Evaluate` with `options`, on up to `options.threads` threads, to the same bits as one thread.
EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid,
                          const EvaluationOptions& options);

/// The margin, in world units, the terrain's filters need around a grid evaluated at `spacing` for
/// its cells to equal those of any larger grid: the largest `Filter::Margin` for the layers'
/// largest feature; 0 without filters.
double FilterMargin(const Terrain& terrain, double spacing);

/// One tile of `scheme` at its level's spacing, as a piece of the unbounded terrain: evaluated with
/// the filters' margin around it and cropped, so tiles equal the same cells of any larger grid on
/// the same lattice (to the bit where cell centres coincide exactly, such as with power-of-two
/// spacings). The features are those placed while evaluating, margin included.
/// @throws std::invalid_argument for an invalid scheme or terrain.
EvaluatedTerrain EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                              unsigned threads = 1);

/// `EvaluateTile` for each of `keys`, tiles split over up to `threads` threads (0 for every
/// hardware thread), in the order of `keys`.
std::vector<EvaluatedTerrain> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                            const std::vector<TileKey>& keys, unsigned threads = 0);

/// Evaluates `terrain` over its source's grid.
EvaluatedTerrain Evaluate(const Terrain& terrain);

/// The height of `terrain` at (`x`, `y`) sampled every `spacing` world units: the source's height
/// there, then each layer's `ApplyAt`. It equals the cell centred there of `Evaluate` over a grid
/// of `spacing` cells, to the same bits, and is safe to call from several threads at once, as point
/// queries from a physics model are.
/// @throws std::invalid_argument if `terrain` has no source, a null layer or any filter (filters
/// read neighbouring cells), or `spacing` is not positive and finite; and whatever the parts throw.
float EvaluateAt(const Terrain& terrain, double x, double y, double spacing);

}  // namespace terraingen
