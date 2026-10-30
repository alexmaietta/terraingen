// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Terrain`, a surface, optional starting heights and layers, evaluated over grids or at
/// points, updated in place and reseeded.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/gpu/backend.h"
#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/ground.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/span.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

class EvaluationCache;
class StageCache;

/// How to evaluate a terrain over a grid.
struct EvaluationOptions {
    /// Sample spacing in meters; 0 means the grid's spacing on the terrain's surface, and a
    /// smaller value is raised to it.
    double spacing = 0.0;
    /// Threads to split each layer over; 0 for every hardware thread. The result does not
    /// depend on it.
    unsigned threads = 0;
    /// Where the grid is evaluated; empty for `DefaultBackend()`. `Gpu` picks the best GPU backend
    /// available and `Auto` the same or the CPU. Results on a GPU match the CPU to within float
    /// rounding, not to the bit, and point queries always run on the CPU.
    std::optional<Backend> backend = std::nullopt;
    /// Called after each layer is applied with its index and the milliseconds it took, reads of
    /// the ground beneath it included; may be empty.
    std::function<void(std::size_t layer, double milliseconds)> onLayer{};
    /// Heights kept before one layer, which the evaluation restarts from when they are still
    /// current and refreshes otherwise; null keeps none. It must outlive the evaluation.
    StageCache* stageCache = nullptr;

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
    /// Sets the stage cache the evaluation restarts from and refreshes.
    EvaluationOptions& Keep(StageCache& cache) {
        stageCache = &cache;
        return *this;
    }
};

/// The heights a terrain held before one of its layers when it was last evaluated over a grid,
/// so evaluating the same grid at the same spacing again after `Terrain::SetLayer` of that layer
/// or a later one applies only the layers from there on. It holds one grid of floats, and the
/// caller owns it; one evaluation may use it at a time.
class StageCache {
  public:
    /// A cache of the heights before layer `stage`: the source's heights for 0, the finished
    /// heights for the terrain's layer count.
    explicit StageCache(std::size_t stage) : stage_(stage) {}

    /// The index of the layer whose input the cache keeps.
    std::size_t Stage() const {
        return stage_;
    }

    /// Whether the cache holds heights.
    bool Empty() const {
        return heights_.empty();
    }

    /// Drops the heights held, freeing their memory.
    void Clear();

  private:
    friend class Terrain;

    std::size_t stage_;
    RasterGrid grid_{};
    double spacing_ = 0.0;
    Backend backend_ = Backend::Cpu;
    std::vector<std::uint64_t> versions_;
    std::vector<float> heights_;
};

/// A terrain: heights from a source (the datum, 0 m, without one), plus each of its layers in
/// order, on a surface.
///
/// Every part is a pure function of where it is evaluated, so a terrain can be evaluated over
/// any grid at any sample spacing, or at scattered points, to the same bits. On a body, grids
/// are in longitude and latitude and spacings in meters (`RasterGrid::Spacing`), which reach the
/// source in its units, divided by `Surface::UnitLength`. A terrain is a `Ground`, and so is each
/// of its prefixes (`Ground`). Copies share their parts, which are immutable; `SetLayer` and
/// `SetSource` replace a part of one terrain only. Evaluation is thread-safe; updates are not.
class Terrain final : public Ground {
  public:
    /// A terrain on a plane without a source or layers.
    Terrain();

    /// A terrain on `surface` with `source` as its starting heights, if any, and `layers`.
    /// @throws std::invalid_argument for a source in longitude and latitude on a plane or a
    /// projected source on a body (`Source::Keys`), a null layer, a layer built for another
    /// surface (`Layer::LayerSurface`), or a layer whose `Layer::Dependencies` do not all come
    /// before it.
    Terrain(terraingen::Surface surface, std::shared_ptr<const terraingen::Source> source,
            std::vector<LayerPtr> layers);

    /// What the terrain lies on.
    const terraingen::Surface& Surface() const {
        return surface_;
    }

    /// The starting heights; null for none.
    const std::shared_ptr<const terraingen::Source>& Source() const {
        return source_;
    }

    /// The layers, applied in order.
    const std::vector<LayerPtr>& Layers() const {
        return layers_;
    }

    /// The grid of the source; none without a source or for one without bounds (`BoundedGrid`).
    std::optional<RasterGrid> Grid() const;

    /// The projection a raster of the terrain's heights is written with: its source's, or
    /// geographic keys on its body (`GeographicSphereKeys`) without one.
    GeoKeys Keys() const;

    /// The heights over `grid`, evaluated on several threads to the same bits as one.
    /// @throws std::invalid_argument for a negative or non-finite spacing, or a stage cache of a
    /// layer past the last; and whatever the parts throw.
    /// @throws std::runtime_error when a GPU backend is requested but unavailable
    /// (`ResolveBackend`).
    Raster<float> Evaluate(const RasterGrid& grid, const EvaluationOptions& options = {}) const;

    /// The heights over the source's grid (`Grid`).
    /// @throws std::invalid_argument as `Evaluate` over a grid, or for a terrain without a
    /// bounded source.
    Raster<float> Evaluate() const;

    /// `Evaluate` over `grid` straight into a caller buffer, to the same bits.
    /// @param heights grid.width * grid.height heights in meters, row-major with row 0 first
    /// (north up), as `Raster` stores them.
    /// @throws std::invalid_argument as `Evaluate`, or for a buffer of another size.
    /// @throws std::runtime_error as `Evaluate`.
    void Fill(const RasterGrid& grid, Span<float> heights,
              const EvaluationOptions& options = {}) const;

    using terraingen::Ground::HeightsAt;

    /// `HeightsAt` split over `threads` threads (0 for every hardware thread); the ground's own
    /// `HeightsAt` uses every one. Points are taken in batches of nearby points, which share
    /// values such as crater datums with every point query of this terrain.
    /// @param spacing Sample spacing in meters; positive and finite.
    /// @throws std::invalid_argument for spans of different sizes or a spacing that is not
    /// positive and finite.
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing,
                   unsigned threads) const;

    /// The craters the layers apply over `grid` (`CraterPlacer::CratersOver`).
    /// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the
    /// grid's.
    CraterFeatures Craters(const RasterGrid& grid, double spacing = 0.0) const;

    /// The boulders the layers place over `grid`, each read on the ground beneath its layer
    /// (`BoulderPlacer::BouldersOver`), and the boulder models among them.
    /// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the
    /// grid's.
    BoulderFeatures Boulders(const RasterGrid& grid, double spacing = 0.0) const;

    /// The terrain beneath layer `stages`: the source and the first `stages` layers, sharing
    /// their versions and point-query values with this terrain.
    /// @throws std::invalid_argument for more stages than layers.
    Terrain Ground(std::size_t stages) const;

    /// Replaces layer `index` with `layer`; later layers that read the old one are rebound to
    /// it (`Layer::Rebound`).
    /// @throws std::invalid_argument for an index past the last layer, or as the constructor;
    /// the terrain is then unchanged.
    void SetLayer(std::size_t index, LayerPtr layer);

    /// Replaces the source; null for none.
    /// @throws std::invalid_argument as the constructor; the terrain is then unchanged.
    void SetSource(std::shared_ptr<const terraingen::Source> source);

    /// A stamp that changes whenever layer `index` is replaced, unique among the parts of every
    /// terrain built; copies and prefixes share it.
    /// @throws std::out_of_range for an index past the last layer.
    std::uint64_t LayerVersion(std::size_t index) const;

    /// A stamp that changes whenever the source is replaced, as `LayerVersion`.
    std::uint64_t SourceVersion() const {
        return sourceVersion_;
    }

    /// A copy whose seeded layers (`Layer::Reseeded`) and source (`Source::Reseeded`) draw from
    /// `seed`, each part from its own seed derived from it, for domain randomization without
    /// reading any data again; layers reading a reseeded layer are rebound to it.
    /// @throws std::invalid_argument as the constructor.
    Terrain Reseed(std::uint64_t seed) const;

  protected:
    /// The heights at `samples` on every hardware thread, as `HeightsAt`.
    void FillSamples(SampleBlock& samples, double spacing) const override;

  private:
    /// Evaluates the terrain into every cell of `heights`, placed by its transform.
    void EvaluateInto(Raster<float>& heights, const EvaluationOptions& options) const;

    /// Applies layers [`first`, `last`) to `heights` at `spacing` meters.
    void ApplyLayers(std::size_t first, std::size_t last, Raster<float>& heights, double spacing,
                     Backend backend, const EvaluationOptions& options) const;

    /// The versions the heights before layer `stage` depend on: the source's and those of the
    /// layers before it.
    std::vector<std::uint64_t> StageVersions(std::size_t stage) const;

    /// Checks the parts, then gives fresh versions to the source if `sourceChanged` and to the
    /// layers at `changed`, and drops the point-query values.
    void Commit(bool sourceChanged, const std::vector<std::size_t>& changed);

    terraingen::Surface surface_{};
    std::shared_ptr<const terraingen::Source> source_;
    std::vector<LayerPtr> layers_;
    std::uint64_t sourceVersion_ = 0;
    std::vector<std::uint64_t> layerVersions_;
    std::shared_ptr<EvaluationCache> cache_;
};

/// Calls `visit(layer, index)` for each layer of `terrain` that is a `T`, in order, with `index`
/// its position in `terrain.Layers()`.
template <typename T, typename Visit>
void ForEachLayer(const Terrain& terrain, const Visit& visit) {
    const std::vector<LayerPtr>& layers = terrain.Layers();
    for (std::size_t n = 0; n < layers.size(); ++n) {
        if (const auto* layer = dynamic_cast<const T*>(layers[n].get())) {
            visit(*layer, n);
        }
    }
}

}  // namespace terraingen
