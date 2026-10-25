// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A terrain generator as optional starting heights and layers, evaluated over grids or at
/// points.
#pragma once

#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/span.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen {

/// A terrain generator as a composition: heights from `source` (the datum, 0 m, without one), plus
/// each of `layers` in order.
///
/// Every part is a pure function of where it is evaluated, so a generator can be evaluated over any
/// grid at any sample spacing. On a body, grids are in longitude and latitude and
/// spacings in meters (`RasterGrid::Spacing`), which reach the source in its units, divided by
/// `Surface::UnitLength`. The layers must be built for `surface`.
struct TerrainGenerator {
    std::shared_ptr<const Source> source;              ///< The starting heights; may be null.
    std::vector<std::shared_ptr<const Layer>> layers;  ///< Applied in order.
    Surface surface{};                                 ///< What the terrain lies on.
};

/// How to evaluate a generator over a grid.
struct EvaluationOptions {
    /// Sample spacing in meters; 0 means the grid's spacing on the generator's surface, and a
    /// smaller value is raised to it.
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

    /// Sets the sample spacing in meters, for chaining: `EvaluationOptions{}.Spacing(0.5)`.
    EvaluationOptions& Spacing(double meters) {
        spacing = meters;
        return *this;
    }
    /// Sets the threads to split each layer over; 0 for every hardware thread.
    EvaluationOptions& Threads(unsigned count) {
        threads = count;
        return *this;
    }
    /// Sets where the grid is evaluated.
    EvaluationOptions& On(Backend where) {
        backend = where;
        return *this;
    }
};

/// The ground beneath one layer of a generator: its starting heights and the layers before it,
/// evaluated at points as `HeightsAt`. `Evaluate` and `HeightsAt` give each layer the ground
/// beneath it.
class TerrainGround final : public Ground {
  public:
    /// The ground beneath layer `layers` of `generator`, which must outlive it.
    /// @param layers The number of layers the ground includes.
    /// @param cache Values the layers beneath share with the evaluation reading this ground, such
    /// as crater datums, so nested reads do not compute them again; null for a cache per read.
    /// @param threads Threads `HeightsAt` splits its batches over; 0 for every hardware thread.
    /// @throws std::invalid_argument if `generator` has fewer than `layers` layers.
    TerrainGround(const TerrainGenerator& generator, std::size_t layers,
                  EvaluationCache* cache = nullptr, unsigned threads = 0);

    /// The starting height at surface point (`x`, `y`) after each included layer's `ApplyAt`, at
    /// a sample spacing of `spacing` meters.
    float HeightAt(double x, double y, double spacing) const override;

    /// The heights at `points` in batches over the ground's threads, as the free `HeightsAt` does.
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const override;

  private:
    const TerrainGenerator& generator_;
    std::size_t layers_;
    EvaluationCache* cache_;
    unsigned threads_;
};

/// A `TerrainGround` beneath the first `layers` layers of a copy of `generator` that it keeps, so
/// it outlives `generator`; the copy shares its source and layers.
/// @throws std::invalid_argument if `generator` has fewer layers.
std::shared_ptr<const Ground> SharedGround(const TerrainGenerator& generator, std::size_t layers);

/// Calls `visit(layer, index)` for each layer of `generator` that is a `T`, in order, with `index`
/// its position in `generator.layers`.
template <typename T, typename Visit>
void ForEachLayer(const TerrainGenerator& generator, const Visit& visit) {
    for (std::size_t n = 0; n < generator.layers.size(); ++n) {
        if (const auto* layer = dynamic_cast<const T*>(generator.layers[n].get())) {
            visit(*layer, n);
        }
    }
}

/// The heights of `generator` over `grid`, evaluated on every hardware thread with the default
/// options.
/// @param spacing Sample spacing in meters; 0 means the grid's spacing, and a smaller value is
/// raised to it.
/// @throws std::invalid_argument as `ValidateGenerator`, or for a negative or non-finite `spacing`;
/// and whatever the parts throw.
Raster<float> Evaluate(const TerrainGenerator& generator, const RasterGrid& grid,
                       double spacing = 0.0);

/// The heights of `generator` over `grid`, evaluated on several threads to the same bits as one.
/// @throws std::invalid_argument as `Evaluate` with a spacing.
/// @throws std::runtime_error when the backend is `Vulkan` and Vulkan is unavailable.
Raster<float> Evaluate(const TerrainGenerator& generator, const RasterGrid& grid,
                       const EvaluationOptions& options);

/// Evaluates `generator` over `grid` into a caller buffer, to the same bits as `Evaluate`.
/// @param heights grid.width * grid.height heights in meters, row-major with row 0 first (north
/// up), as `Raster` stores them.
/// @throws std::invalid_argument as `Evaluate`, or for a buffer of another size.
/// @throws std::runtime_error as `Evaluate`.
void FillHeights(const TerrainGenerator& generator, const RasterGrid& grid, Span<float> heights,
                 const EvaluationOptions& options = {});

/// Checks that a generator can be evaluated.
/// @throws std::invalid_argument if `generator` holds a source in longitude and latitude on a
/// plane or a projected source on a body (`Source::Keys`), a null layer, a layer built for
/// another surface than `generator.surface` (`Layer::LayerSurface`), or a layer whose
/// `Layer::Dependencies` do not all come before it.
void ValidateGenerator(const TerrainGenerator& generator);

/// The projection a raster of `generator`'s heights is written with: its source's, or
/// geographic keys on its body (`GeographicSphereKeys`) without one.
GeoKeys TerrainGeoKeys(const TerrainGenerator& generator);

/// Writes `raster`, evaluated from `generator`, as a GeoTIFF in the terrain's projection
/// (`TerrainGeoKeys`).
/// @throws GeoTiffError as `WriteGeoTiff`.
void WriteGeoTiff(const std::filesystem::path& path, const Raster<float>& raster,
                  const TerrainGenerator& generator);

/// The heights of `generator` over its source's grid.
/// @throws std::invalid_argument as `Evaluate` over a grid, or for a generator without a source or
/// with a source without bounds (`BoundedGrid`).
Raster<float> Evaluate(const TerrainGenerator& generator);

/// The heights of a generator at scattered points, such as wheel contacts or mesh vertices: the
/// starting heights there, then each layer applied to all of them at once. Each equals the cell
/// centered at its point of `Evaluate` over a grid of `spacing` meters, to the same bits, whatever
/// the batch and thread count.
/// @param points Positions in surface coordinates.
/// @param spacing The sample spacing, in meters.
/// @param threads Threads to split the points over; 0 for every hardware thread.
/// @param cache Values kept between calls, such as fitted crater datums; null shares a cache kept
/// for this generator, dropped once one of its parts is freed or it goes unqueried longest. A
/// cache passed in belongs to one generator: start a new one after changing its source or layers.
/// @throws std::invalid_argument as `ValidateGenerator`, or for a `spacing` that is not positive
/// and finite.
std::vector<float> HeightsAt(const TerrainGenerator& generator, Span<const Vec2d> points,
                             double spacing, unsigned threads = 0,
                             EvaluationCache* cache = nullptr);

/// The height of a generator at one point, as `HeightsAt`.
/// @throws std::invalid_argument as `HeightsAt`.
float HeightAt(const TerrainGenerator& generator, double x, double y, double spacing,
               EvaluationCache* cache = nullptr);

/// The craters a terrain's layers apply over `grid`: `PlacedCraters` of its layers.
/// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the grid's.
CraterFeatures PlacedCraters(const TerrainGenerator& generator, const RasterGrid& grid,
                             double spacing = 0.0);

/// The boulders a terrain's `BoulderPlacer` layers place over `grid`, each read on the ground
/// beneath its layer, and the boulder models among them.
/// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the grid's.
BoulderFeatures PlacedBoulders(const TerrainGenerator& generator, const RasterGrid& grid,
                               double spacing = 0.0);

}  // namespace terraingen
