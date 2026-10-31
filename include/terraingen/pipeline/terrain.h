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

/// How to evaluate a terrain over a grid.
struct EvaluationOptions {
    /// Sample spacing in meters; 0 means the grid's spacing on the terrain's surface, and a
    /// smaller value is raised to it.
    double spacing = 0.0;
    /// Threads to split each layer over; 0 for every hardware thread. The result does not
    /// depend on it.
    unsigned threads = 0;
    /// Where the grid is evaluated; empty for `DefaultBackend()`. GPU results match the CPU to
    /// float rounding; point queries always run on the CPU.
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

/// A surface, a source (0 m without one) and layers applied in order.
///
/// Grids, tiles and points give the same bits. On a body, grids are in longitude and latitude
/// and spacings in meters. Copies share their immutable parts; evaluation is thread-safe, updates
/// are not.
class Terrain final : public Ground {
  public:
    /// A terrain on a plane without a source or layers.
    Terrain();

    /// A terrain on `surface` with `source` as its starting heights, if any, and `layers`.
    /// @throws std::invalid_argument for a source in longitude and latitude on a plane or a
    /// projected source on a body (`Source::Keys`), a null layer, a layer built on another surface
    /// or on a source other than the terrain's (`Layer::LayerBase`; one built on none applies no
    /// source limits), or a layer whose `Layer::Dependencies` do not all come before it.
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

    /// The context a layer reads the terrain in: its surface and source, on this terrain.
    LayerContext Context(double spacing, unsigned threads) const override;

    /// The heights over `grid`, evaluated on several threads to the same bits as one.
    /// @throws std::invalid_argument for a negative or non-finite spacing; and whatever the parts
    /// throw.
    /// @throws std::runtime_error when a GPU backend is requested but unavailable
    /// (`ResolveBackend`).
    Raster<float> Evaluate(const RasterGrid& grid, const EvaluationOptions& options = {}) const;

    /// The heights over the source's grid (`Grid`).
    /// @throws std::invalid_argument as `Evaluate` over a grid, or for a terrain without a
    /// bounded source.
    /// @throws std::runtime_error as `Evaluate` over a grid.
    Raster<float> Evaluate(const EvaluationOptions& options = {}) const;

    /// `Evaluate` over `grid` straight into a caller buffer, to the same bits.
    /// @param heights grid.width * grid.height heights in meters, row-major with row 0 first
    /// (north up), as `Raster` stores them.
    /// @throws std::invalid_argument as `Evaluate`, or for a buffer of another size.
    /// @throws std::runtime_error as `Evaluate`.
    void Fill(const RasterGrid& grid, Span<float> heights,
              const EvaluationOptions& options = {}) const;

    /// Applies the layers from `first` on to `heights`, which hold the heights of
    /// `Beneath(first)` over their grid evaluated with `options`, to the bits of `Evaluate`.
    /// @throws std::invalid_argument for a `first` past the layer count, or as `Evaluate`.
    /// @throws std::runtime_error as `Evaluate`.
    void ApplyFrom(std::size_t first, Raster<float>& heights,
                   const EvaluationOptions& options = {}) const;

    /// The craters the layers apply over `grid` (`CraterPlacer::CratersOver`).
    /// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the
    /// grid's.
    CraterFeatures Craters(const RasterGrid& grid, double spacing = 0.0) const;

    /// The craters the layers apply over the source's grid (`Grid`).
    /// @param spacing As for `Craters` over a grid.
    /// @throws std::invalid_argument for a terrain without a bounded source.
    CraterFeatures Craters(double spacing = 0.0) const;

    /// The boulders the layers place over `grid`, each read on the ground beneath its layer
    /// (`BoulderPlacer::BouldersOver`), and the boulder models among them.
    /// @param spacing The sample spacing in meters, finer than the grid's or not; 0 for the
    /// grid's.
    BoulderFeatures Boulders(const RasterGrid& grid, double spacing = 0.0) const;

    /// The boulders the layers place over the source's grid (`Grid`).
    /// @param spacing As for `Boulders` over a grid.
    /// @throws std::invalid_argument for a terrain without a bounded source.
    BoulderFeatures Boulders(double spacing = 0.0) const;

    /// The terrain beneath layer `layers`: the source and the first `layers` layers, sharing
    /// their point-query values with this terrain.
    /// @throws std::invalid_argument for more layers than the terrain has.
    Terrain Beneath(std::size_t layers) const;

    /// Replaces layer `index` with `layer`; later layers that read the old one are rebound to
    /// it (`Layer::Rebound`).
    /// @throws std::invalid_argument for an index past the last layer, or as the constructor;
    /// the terrain is then unchanged.
    void SetLayer(std::size_t index, LayerPtr layer);

    /// Replaces the source, null for none; layers built on the old one are rebuilt on the new
    /// (`Layer::Rebased`) and later layers reading them rebound to them.
    /// @throws std::invalid_argument as the constructor; the terrain is then unchanged.
    void SetSource(std::shared_ptr<const terraingen::Source> source);

    /// A copy whose seeded layers (`Layer::Reseeded`) and source (`Source::Reseeded`) draw from
    /// `seed`, each part from its own seed derived from it, for domain randomization without
    /// reading any data again; layers are rebased on a reseeded source and rebound to reseeded
    /// layers they read.
    /// @throws std::invalid_argument as the constructor.
    Terrain Reseed(std::uint64_t seed) const;

  protected:
    /// The heights at `samples`, as `HeightsAt`: points in batches of nearby points, which share
    /// values such as crater datums with every point query of this terrain.
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override;

  private:
    /// The replacement of layer `index` of the terrain, `layer`; null keeps it.
    using LayerReplacer = std::function<LayerPtr(std::size_t index, const LayerPtr& layer)>;

    /// Replaces each layer `replace` gives a replacement of and the source with `source`,
    /// rebases the layers built on the old source (`Layer::Rebased`) and rebinds those reading a
    /// replaced layer, then checks the terrain, which is unchanged when that throws.
    void Replace(std::shared_ptr<const terraingen::Source> source, const LayerReplacer& replace);

    /// The grid of the source.
    /// @throws std::invalid_argument for a terrain without a bounded source.
    RasterGrid SourceGrid() const;

    terraingen::Surface surface_{};
    std::shared_ptr<const terraingen::Source> source_;
    std::vector<LayerPtr> layers_;
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
