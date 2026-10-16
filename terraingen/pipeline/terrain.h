/// @file
/// @brief A terrain as a source, layers and filters, evaluated over grids, tiles, points or a
/// whole body.
#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/layer.h"
#include "terraingen/meshes/mesh.h"
#include "terraingen/pipeline/tiling.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <memory>
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
/// evaluated at single points with `ApplyAt`. `Evaluate` and `EvaluateAt` give each layer the
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

/// @brief One tile of a scheme at its level's spacing, as a piece of the unbounded terrain.
///
/// The tile is evaluated with the filters' margin around it and cropped, so it equals the same
/// cells of any larger grid on the same lattice (to the bit where cell centres coincide exactly,
/// such as with power-of-two spacings). The features are those placed while evaluating, margin
/// included.
/// @param terrain The terrain.
/// @param scheme The tile scheme.
/// @param key The tile.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The tile's heights and features.
/// @throws std::invalid_argument for an invalid scheme, level or terrain; and whatever the parts
/// throw.
EvaluatedTerrain EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                              unsigned threads = 1);

/// @brief `EvaluateTile` for several tiles, split over threads.
/// @param terrain The terrain.
/// @param scheme The tile scheme.
/// @param keys The tiles.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The tiles, in the order of `keys`.
/// @throws std::invalid_argument as `EvaluateTile`.
std::vector<EvaluatedTerrain> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                            const std::vector<TileKey>& keys, unsigned threads = 0);

/// @brief Evaluates a terrain over its source's grid.
/// @param terrain The terrain.
/// @return The heights and features.
/// @throws std::invalid_argument as `Evaluate` over a grid.
EvaluatedTerrain Evaluate(const Terrain& terrain);

/// @brief A mesh of the whole body a terrain on a sphere lies on.
///
/// The six faces of a cube are each split into `cellsPerFace` x `cellsPerFace` quads along
/// equiangular coordinates (as `CubeSphereCells`), two triangles each, with every vertex at the
/// body's radius plus the terrain's height there (`EvaluateAt`; points without data are on the
/// datum). Positions are body-fixed metres, x towards longitude 0 on the equator and z north
/// (`Surface::Position`); vertices along the faces' edges are repeated per face. Filters read
/// neighbouring cells, so they are left out.
/// @param terrain The terrain, on a sphere.
/// @param cellsPerFace Quads along each edge of a face.
/// @param spacing Sample spacing in metres; 0 for a quad's side at a face's centre.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The mesh.
/// @throws std::invalid_argument for a terrain not on a sphere, zero cells, or a negative or
/// non-finite spacing; and whatever the terrain's parts throw.
TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing = 0.0,
                      unsigned threads = 0);

/// @brief The height of a terrain at one point: the source's height there, then each layer's
/// `ApplyAt`. It equals the cell centred there of `Evaluate` over a grid of `spacing` cells, to the
/// same bits, and is safe to call from several threads at once.
/// @param terrain The terrain, without filters.
/// @param x The point's x, in surface coordinates.
/// @param y The point's y, in surface coordinates.
/// @param spacing The sample spacing, in metres.
/// @return The height.
/// @throws std::invalid_argument if `terrain` has no source, a null layer or any filter (filters
/// read neighbouring cells), or `spacing` is not positive and finite; and whatever the parts throw.
float EvaluateAt(const Terrain& terrain, double x, double y, double spacing);

}  // namespace terraingen
