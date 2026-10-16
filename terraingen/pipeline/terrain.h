/// @file
/// @brief A terrain as a source, layers and filters, evaluated over grids or at points.
#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace terraingen {

/// @brief A terrain as a composition: heights from `source`, plus each of `layers` in order, then
/// each of `filters` in order.
///
/// Every part is a pure function of where it is evaluated, so a terrain can be evaluated over any
/// grid on its source's lattice, such as a tile, at any sample spacing. On a sphere, grids are in
/// longitude and latitude and spacings in metres (`RasterGrid::Spacing`). The layers and filters
/// must be built for `surface`.
struct Terrain {
    std::shared_ptr<const Source> source;                ///< The starting heights.
    std::vector<std::shared_ptr<const Layer>> layers;    ///< Applied in order.
    std::vector<std::shared_ptr<const Filter>> filters;  ///< Applied in order after the layers.
    Surface surface{};                                   ///< What the terrain lies on.
};

/// @brief How to evaluate a terrain over a grid.
struct EvaluationOptions {
    /// Sample spacing in metres; 0 means the grid's spacing on the terrain's surface, and a smaller
    /// value is raised to it.
    double spacing = 0.0;
    /// Threads to split each layer and filter over; 0 for every hardware thread. The result does
    /// not depend on it.
    unsigned threads = 0;
};

/// @brief The ground beneath one layer of a terrain: its source and the layers before it,
/// evaluated at points as `EvaluatePoints`. `Evaluate` and `EvaluatePoints` give each layer the
/// ground beneath it.
class TerrainGround final : public Ground {
  public:
    /// @brief The ground beneath layer `layers` of a terrain.
    /// @param terrain The terrain; it must outlive the ground.
    /// @param layers The number of layers the ground includes.
    /// @throws std::invalid_argument if `terrain` has no source or fewer than `layers` layers.
    TerrainGround(const Terrain& terrain, std::size_t layers);

    /// @brief The ground's height at a point.
    /// @param x The point's x, in surface coordinates.
    /// @param y The point's y, in surface coordinates.
    /// @param spacing The sample spacing, in metres.
    /// @return The source's height there after each included layer's `ApplyAt`.
    float HeightAt(double x, double y, double spacing) const override;

  private:
    const Terrain& terrain_;
    std::size_t layers_;
};

/// @brief Heights over a grid and the features the layers placed there.
struct EvaluatedTerrain {
    Raster<float> heights;     ///< The heights.
    TerrainFeatures features;  ///< Craters, boulders and boulder models placed.
};

/// @brief Evaluates a terrain over a grid on one thread.
/// @param terrain The terrain.
/// @param grid The grid.
/// @param spacing Sample spacing in metres; 0 means the grid's spacing, and a smaller value is
/// raised to it.
/// @return The heights and features.
/// @throws std::invalid_argument if `terrain` has no source or holds a null layer or filter, or
/// `spacing` is negative or not finite; and whatever the parts throw.
EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing = 0.0);

/// @brief Evaluates a terrain over a grid on several threads, to the same bits as one thread.
/// @param terrain The terrain.
/// @param grid The grid.
/// @param options The sample spacing and threads.
/// @return The heights and features.
/// @throws std::invalid_argument as the single-threaded `Evaluate`.
EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid,
                          const EvaluationOptions& options);

/// @brief The margin a terrain's filters need around a grid for its cells to equal those of any
/// larger grid: the largest `Filter::Margin` for the layers' largest feature.
/// @param terrain The terrain.
/// @param spacing The sample spacing, in metres.
/// @return The margin in metres; 0 without filters.
/// @throws std::invalid_argument if `terrain` has no source or holds a null layer or filter.
double FilterMargin(const Terrain& terrain, double spacing);

/// @brief Evaluates a terrain over its source's grid.
/// @param terrain The terrain.
/// @return The heights and features.
/// @throws std::invalid_argument as `Evaluate` over a grid.
EvaluatedTerrain Evaluate(const Terrain& terrain);

/// The heights of a terrain at scattered points, such as wheel contacts or mesh vertices: the
/// source's heights there, then each layer applied to all of them at once. Each equals the cell
/// centred at its point of `Evaluate` over a grid of `spacing` metres, to the same bits, whatever
/// the batch and thread count.
/// @param points Positions in surface coordinates.
/// @param spacing The sample spacing, in metres.
/// @param threads Threads to split the points over; 0 for every hardware thread.
/// @throws std::invalid_argument if `terrain` has no source, a null layer or any filter (filters
/// read neighbouring cells), or `spacing` is not positive and finite.
std::vector<float> EvaluatePoints(const Terrain& terrain, std::span<const Vec2d> points,
                                  double spacing, unsigned threads = 0);

/// The height of a terrain at one point, as `EvaluatePoints`.
/// @throws std::invalid_argument as `EvaluatePoints`.
float EvaluateAt(const Terrain& terrain, double x, double y, double spacing);

}  // namespace terraingen
