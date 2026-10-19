/// @file
/// @brief A terrain as optional starting heights and layers, evaluated over grids or at points.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen {

/// A terrain as a composition: heights from `source` (the datum, 0 m, without one), plus each of
/// `layers` in order.
///
/// Every part is a pure function of where it is evaluated, so a terrain can be evaluated over any
/// grid, such as a tile, at any sample spacing. On a body, grids are in longitude and latitude and
/// spacings in metres (`RasterGrid::Spacing`). The layers must be built for `surface`.
struct Terrain {
    std::shared_ptr<const Source> source;              ///< The starting heights; may be null.
    std::vector<std::shared_ptr<const Layer>> layers;  ///< Applied in order.
    Surface surface{};                                 ///< What the terrain lies on.
};

/// How to evaluate a terrain over a grid.
struct EvaluationOptions {
    /// Sample spacing in metres; 0 means the grid's spacing on the terrain's surface, and a smaller
    /// value is raised to it.
    double spacing = 0.0;
    /// Threads to split each layer over; 0 for every hardware thread. The result does
    /// not depend on it.
    unsigned threads = 0;
    /// Where the grid is evaluated; empty for `DefaultBackend()`. `Gpu` picks the best GPU backend
    /// available and `Auto` the same or the CPU. Results on a GPU match the CPU to within float
    /// rounding, not to the bit, and point queries always run on the CPU.
    std::optional<Backend> backend = std::nullopt;
    /// Called after each layer is applied with its index and the milliseconds it took, reads of
    /// the ground beneath it included; may be empty.
    std::function<void(std::size_t layer, double milliseconds)> onLayer{};
};

/// The ground beneath one layer of a terrain: its starting heights and the layers before it,
/// evaluated at points as `EvaluatePoints`. `Evaluate` and `EvaluatePoints` give each layer the
/// ground beneath it.
class TerrainGround final : public Ground {
  public:
    /// The ground beneath layer `layers` of `terrain`, which must outlive it.
    /// @param layers The number of layers the ground includes.
    /// @param cache Values the layers beneath share with the evaluation reading this ground, such
    /// as crater datums, so nested reads do not compute them again; null for a cache per read.
    /// @throws std::invalid_argument if `terrain` has fewer than `layers` layers.
    TerrainGround(const Terrain& terrain, std::size_t layers, EvaluationCache* cache = nullptr);

    /// The starting height at surface point (`x`, `y`) after each included layer's `ApplyAt`, at
    /// a sample spacing of `spacing` metres.
    float HeightAt(double x, double y, double spacing) const override;

    /// The heights at `points` in batches over every hardware thread, as `EvaluatePoints` does.
    void HeightsAt(std::span<const Vec2d> points, std::span<float> heights,
                   double spacing) const override;

  private:
    const Terrain& terrain_;
    std::size_t layers_;
    EvaluationCache* cache_;
};

/// A `TerrainGround` beneath the first `layers` layers of a copy of `terrain` that it keeps, so it
/// outlives `terrain`; the copy shares its source and layers.
/// @throws std::invalid_argument if `terrain` has fewer layers.
std::shared_ptr<const Ground> SharedGround(const Terrain& terrain, std::size_t layers);

/// Layer `index` of `terrain`, which must be a `T`.
/// @param what What a `T` is, for the error message, such as "a boulder layer".
/// @throws std::invalid_argument if `terrain` has no layer `index` or it is not a `T`.
template <typename T>
std::shared_ptr<const T> LayerAt(const Terrain& terrain, std::size_t index, std::string_view what) {
    if (index >= terrain.layers.size()) {
        throw std::invalid_argument("The terrain has no layer " + std::to_string(index));
    }
    auto layer = std::dynamic_pointer_cast<const T>(terrain.layers[index]);
    if (!layer) {
        throw std::invalid_argument("Layer " + std::to_string(index) + " is not " +
                                    std::string(what));
    }
    return layer;
}

/// Calls `visit(layer, index)` for each layer of `terrain` that is a `T`, in order, with `index`
/// its position in `terrain.layers`.
template <typename T, typename Visit>
void ForEachLayer(const Terrain& terrain, const Visit& visit) {
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        if (const auto* layer = dynamic_cast<const T*>(terrain.layers[n].get())) {
            visit(*layer, n);
        }
    }
}

/// The heights of `terrain` over `grid`, evaluated on every hardware thread with the default
/// options.
/// @param spacing Sample spacing in metres; 0 means the grid's spacing, and a smaller value is
/// raised to it.
/// @throws std::invalid_argument as `ValidateTerrain`, or for a negative or non-finite `spacing`;
/// and whatever the parts throw.
Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing = 0.0);

/// The heights of `terrain` over `grid`, evaluated on several threads to the same bits as one.
/// @throws std::invalid_argument as `Evaluate` with a spacing.
/// @throws std::runtime_error when the backend is `Vulkan` and Vulkan is unavailable.
Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid,
                       const EvaluationOptions& options);

/// Checks that a terrain can be evaluated.
/// @throws std::invalid_argument if `terrain` holds a null layer.
void ValidateTerrain(const Terrain& terrain);

/// The heights of `terrain` over its source's grid.
/// @throws std::invalid_argument as `Evaluate` over a grid, or for a terrain without a source.
Raster<float> Evaluate(const Terrain& terrain);

/// The heights of a terrain at scattered points, such as wheel contacts or mesh vertices: the
/// starting heights there, then each layer applied to all of them at once. Each equals the cell
/// centred at its point of `Evaluate` over a grid of `spacing` metres, to the same bits, whatever
/// the batch and thread count.
/// @param points Positions in surface coordinates.
/// @param spacing The sample spacing, in metres.
/// @param threads Threads to split the points over; 0 for every hardware thread.
/// @throws std::invalid_argument as `ValidateTerrain`, or for a `spacing` that is not positive
/// and finite.
std::vector<float> EvaluatePoints(const Terrain& terrain, std::span<const Vec2d> points,
                                  double spacing, unsigned threads = 0);

/// The height of a terrain at one point, as `EvaluatePoints`.
/// @throws std::invalid_argument as `EvaluatePoints`.
float EvaluateAt(const Terrain& terrain, double x, double y, double spacing);

}  // namespace terraingen
