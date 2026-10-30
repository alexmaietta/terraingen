// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/terrain.h"

#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Values the point queries of a terrain and its copies keep, such as fitted crater datums.
constexpr std::size_t kPointQueryValues = std::size_t{1} << 17U;

/// A stamp never given before, for a part of a terrain that is new or replaced.
std::uint64_t NextVersion() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

/// The 64-bit seed of part `part` of a terrain reseeded with `seed`: 0 the source, n + 1 layer n.
std::uint64_t PartSeed(std::uint64_t seed, std::size_t part) {
    const auto word = static_cast<std::uint32_t>(2 * part);
    return JoinWords(ReseedWord(seed, word), ReseedWord(seed, word + 1));
}

/// The context of a layer of `terrain`.
LayerContext TerrainContext(const Terrain& terrain, double spacing, unsigned threads,
                            const Ground* ground, Backend backend, EvaluationCache* cache) {
    return LayerContext{
        spacing, threads, ground, backend, cache, &terrain.Surface(), terrain.Source().get()};
}

void EvaluateBatched(const Terrain& terrain, std::size_t layers, Span<const Vec2d> points,
                     Span<float> heights, double spacing, unsigned threads, EvaluationCache* cache);

/// The ground beneath layer `layers` of a terrain during one evaluation: its batches share
/// `cache` and split over `threads` threads.
class StackGround final : public Ground {
  public:
    StackGround(const Terrain& terrain, std::size_t layers, EvaluationCache* cache,
                unsigned threads)
        : terrain_(terrain), layers_(layers), cache_(cache), threads_(threads) {}

  protected:
    void FillSamples(SampleBlock& samples, double spacing) const override {
        if (samples.IsGrid()) {
            const std::vector<Vec2d> positions = samples.Positions();
            EvaluateBatched(terrain_, layers_, positions, samples.Heights(), spacing, threads_,
                            cache_);
            return;
        }
        EvaluateBatched(terrain_, layers_, samples.Points(), samples.Heights(), spacing, threads_,
                        cache_);
    }

  private:
    const Terrain& terrain_;
    std::size_t layers_;
    EvaluationCache* cache_;
    unsigned threads_;
};

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it, which shares `cache` and reads in batches over
/// `threads` threads.
void EvaluateStack(const Terrain& terrain, std::size_t layers, Span<const Vec2d> points,
                   Span<float> heights, double spacing, EvaluationCache* cache, unsigned threads) {
    if (terrain.Source()) {
        terrain.Source()->HeightsAt(points, heights, spacing / terrain.Surface().UnitLength());
    } else {
        std::fill(heights.begin(), heights.end(), 0.0f);
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const StackGround ground(terrain, n, cache, threads);
        SampleBlock samples(points, heights);
        terrain.Layers()[n]->Apply(
            samples, TerrainContext(terrain, spacing, 1, &ground, Backend::Cpu, cache));
    }
}

/// The Morton code of `point` on a 2^16 lattice over `bounds`.
std::uint32_t MortonKey(const Vec2d& point, const Bounds2d& bounds) {
    const auto quantize = [](double value, double low, double high) {
        const double share = high > low ? (value - low) / (high - low) : 0.0;
        return static_cast<std::uint32_t>(std::clamp(share, 0.0, 1.0) * 65535.0);
    };
    const auto spread = [](std::uint32_t v) {
        v = (v | (v << 8u)) & 0x00FF00FFu;
        v = (v | (v << 4u)) & 0x0F0F0F0Fu;
        v = (v | (v << 2u)) & 0x33333333u;
        v = (v | (v << 1u)) & 0x55555555u;
        return v;
    };
    return spread(quantize(point.x, bounds.minX, bounds.maxX)) |
           (spread(quantize(point.y, bounds.minY, bounds.maxY)) << 1u);
}

/// The indices of `points` sorted by their Morton codes over the points' bounds, ties by index.
std::vector<std::size_t> MortonOrder(Span<const Vec2d> points) {
    Bounds2d bounds{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Vec2d& p : points) {
        bounds = {std::fmin(bounds.minX, p.x), std::fmin(bounds.minY, p.y),
                  std::fmax(bounds.maxX, p.x), std::fmax(bounds.maxY, p.y)};
    }
    std::vector<std::size_t> order(points.size());
    if (points.size() > std::numeric_limits<std::uint32_t>::max()) {
        std::vector<std::pair<std::uint32_t, std::size_t>> keyed(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) {
            keyed[i] = {MortonKey(points[i], bounds), i};
        }
        std::sort(keyed.begin(), keyed.end());
        for (std::size_t i = 0; i < points.size(); ++i) {
            order[i] = keyed[i].second;
        }
        return order;
    }
    std::vector<std::uint64_t> keyed(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        keyed[i] = (std::uint64_t{MortonKey(points[i], bounds)} << 32U) | std::uint64_t{i};
    }
    std::sort(keyed.begin(), keyed.end());
    for (std::size_t i = 0; i < points.size(); ++i) {
        order[i] = static_cast<std::size_t>(keyed[i] & 0xFFFFFFFFU);
    }
    return order;
}

/// `EvaluateStack` over `points` in batches on `threads` threads, the points taken in Morton order
/// so each batch covers a small area and prepares only the features there; the batches share
/// `cache`, or one of their own when it is null.
void EvaluateBatched(const Terrain& terrain, std::size_t layers, Span<const Vec2d> points,
                     Span<float> heights, double spacing, unsigned threads,
                     EvaluationCache* cache) {
    constexpr std::size_t kBatch = 256;
    if (points.size() <= kBatch) {
        EvaluateStack(terrain, layers, points, heights, spacing, cache, threads);
        return;
    }
    const std::vector<std::size_t> order = MortonOrder(points);
    std::vector<Vec2d> sorted(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        sorted[i] = points[order[i]];
    }
    std::vector<float> values(points.size());
    std::optional<EvaluationCache> own;
    EvaluationCache* shared = cache != nullptr ? cache : &own.emplace();
    ParallelFor(CeilDiv(points.size(), kBatch), threads, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(terrain, layers, Span<const Vec2d>(sorted).subspan(begin, count),
                      Span<float>(values).subspan(begin, count), spacing, shared, threads);
    });
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[order[i]] = values[i];
    }
}

/// Checks that `layers` on `surface` over `source` can be evaluated.
void Validate(const Surface& surface, const Source* source, const std::vector<LayerPtr>& layers) {
    if (std::any_of(layers.begin(), layers.end(), [](const LayerPtr& l) { return !l; })) {
        throw std::invalid_argument("A terrain's layers must not be null");
    }
    if (source != nullptr) {
        const GeoKeys keys = source->Keys();
        if (keys.IsGeographic() && !surface.IsBody()) {
            throw std::invalid_argument(
                "The source is in longitude and latitude (a geographic CRS); craters are sized in "
                "meters, so reproject it first, for example with gdalwarp -t_srs, or set the "
                "terrain's surface to a sphere or ellipsoid");
        }
        if (surface.IsBody() && !keys.Empty() && !keys.IsGeographic()) {
            throw std::invalid_argument(
                "On a body the source must be in longitude and latitude (a geographic CRS)");
        }
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const Layer& layer = *layers[i];
        const std::optional<Surface> built = layer.LayerSurface();
        if (built && !(*built == surface)) {
            throw std::invalid_argument("Layer " + std::to_string(i) +
                                        " was built for another surface than the terrain's");
        }
        const auto before = layers.begin() + static_cast<std::ptrdiff_t>(i);
        for (const Layer* dependency : layer.Dependencies()) {
            if (std::none_of(layers.begin(), before,
                             [dependency](const LayerPtr& l) { return l.get() == dependency; })) {
                throw std::invalid_argument(
                    "Layer " + std::to_string(i) +
                    " reads a layer that does not come before it in the terrain's layers");
            }
        }
    }
}

/// Rebinds layers of `layers` from `first` on that read a layer of `replaced`
/// (`Layer::Rebound`), adding each one rebound to `replaced` and its index to `changed`.
void RebindFrom(std::size_t first, std::vector<LayerPtr>& layers, LayerReplacements& replaced,
                std::vector<std::size_t>& changed) {
    for (std::size_t n = first; n < layers.size(); ++n) {
        if (LayerPtr rebound = layers[n]->Rebound(replaced)) {
            replaced.emplace_back(layers[n].get(), rebound);
            layers[n] = std::move(rebound);
            changed.push_back(n);
        }
    }
}

}  // namespace

void StageCache::Clear() {
    versions_.clear();
    heights_ = {};
}

Terrain::Terrain() : Terrain(terraingen::Surface{}, nullptr, {}) {}

Terrain::Terrain(terraingen::Surface surface, std::shared_ptr<const terraingen::Source> source,
                 std::vector<LayerPtr> layers)
    : surface_(std::move(surface)), source_(std::move(source)), layers_(std::move(layers)),
      layerVersions_(layers_.size()) {
    std::vector<std::size_t> every(layers_.size());
    for (std::size_t n = 0; n < every.size(); ++n) {
        every[n] = n;
    }
    Commit(true, every);
}

void Terrain::Commit(bool sourceChanged, const std::vector<std::size_t>& changed) {
    Validate(surface_, source_.get(), layers_);
    if (sourceChanged) {
        sourceVersion_ = NextVersion();
    }
    for (const std::size_t n : changed) {
        layerVersions_[n] = NextVersion();
    }
    cache_ = std::make_shared<EvaluationCache>(kPointQueryValues);
}

std::optional<RasterGrid> Terrain::Grid() const {
    return source_ ? BoundedGrid(*source_) : std::nullopt;
}

GeoKeys Terrain::Keys() const {
    GeoKeys keys = source_ ? source_->Keys() : GeoKeys{};
    if (surface_.IsBody() && keys.Empty()) {
        keys = GeographicSphereKeys(surface_.Radius());
    }
    return keys;
}

Raster<float> Terrain::Evaluate(const RasterGrid& grid, const EvaluationOptions& options) const {
    Raster<float> heights = grid.MakeRaster();
    EvaluateInto(heights, options);
    return heights;
}

Raster<float> Terrain::Evaluate() const {
    const std::optional<RasterGrid> grid = Grid();
    if (!grid) {
        throw std::invalid_argument(
            "A terrain without a source, or with a source without bounds such as synthetic "
            "noise, needs a grid to evaluate over");
    }
    return Evaluate(*grid);
}

void Terrain::Fill(const RasterGrid& grid, Span<float> heights,
                   const EvaluationOptions& options) const {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("The grid needs " + std::to_string(grid.width * grid.height) +
                                    " heights, not " + std::to_string(heights.size()));
    }
    Raster<float> view(grid.width, grid.height, heights);
    view.SetTransform(grid.transform);
    EvaluateInto(view, options);
}

std::vector<std::uint64_t> Terrain::StageVersions(std::size_t stage) const {
    std::vector<std::uint64_t> versions{sourceVersion_};
    versions.insert(versions.end(), layerVersions_.begin(),
                    layerVersions_.begin() + static_cast<std::ptrdiff_t>(stage));
    return versions;
}

void Terrain::EvaluateInto(Raster<float>& heights, const EvaluationOptions& options) const {
    RequireNonNegative(options.spacing, "Sample spacing");
    const RasterGrid grid = RasterGrid::FromRaster(heights);
    const double spacing = ResolvedSpacing(grid, options.spacing, surface_);
    const Backend backend = ResolveBackend(options.backend.value_or(DefaultBackend()));
    StageCache* kept = options.stageCache;
    const std::size_t stage = kept != nullptr ? kept->Stage() : layers_.size();
    if (stage > layers_.size()) {
        throw std::invalid_argument("The stage cache keeps the heights before layer " +
                                    std::to_string(stage) + ", but the terrain has " +
                                    std::to_string(layers_.size()) + " layers");
    }
    const std::vector<std::uint64_t> versions =
        kept != nullptr ? StageVersions(stage) : std::vector<std::uint64_t>{};
    const Span<float> values = heights.Values();
    if (kept != nullptr && !kept->Empty() && kept->grid_ == grid && kept->spacing_ == spacing &&
        kept->backend_ == backend && kept->versions_ == versions) {
        std::copy(kept->heights_.begin(), kept->heights_.end(), values.begin());
    } else {
        if (source_) {
            source_->Fill(heights, spacing / surface_.UnitLength(), options.threads);
        } else {
            std::fill(values.begin(), values.end(), 0.0f);
        }
        ApplyLayers(0, stage, heights, spacing, backend, options);
        if (kept != nullptr) {
            kept->grid_ = grid;
            kept->spacing_ = spacing;
            kept->backend_ = backend;
            kept->versions_ = versions;
            kept->heights_.assign(values.begin(), values.end());
        }
    }
    ApplyLayers(stage, layers_.size(), heights, spacing, backend, options);
}

void Terrain::ApplyLayers(std::size_t first, std::size_t last, Raster<float>& heights,
                          double spacing, Backend backend, const EvaluationOptions& options) const {
    for (std::size_t n = first; n < last; ++n) {
        const Stopwatch stopwatch;
        EvaluationCache cache;
        const StackGround ground(*this, n, &cache, options.threads);
        const LayerContext context =
            TerrainContext(*this, spacing, options.threads, &ground, backend, nullptr);
        layers_[n]->Apply(heights, context);
        if (options.onLayer) {
            options.onLayer(n, stopwatch.Milliseconds());
        }
    }
}

void Terrain::FillSamples(SampleBlock& samples, double spacing) const {
    RequirePositive(spacing, "Sample spacing");
    const StackGround ground(*this, layers_.size(), cache_.get(), 0);
    ground.HeightsAt(samples.IsGrid() ? Span<const Vec2d>(samples.Positions()) : samples.Points(),
                     samples.Heights(), spacing);
}

void Terrain::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing,
                        unsigned threads) const {
    RequirePositive(spacing, "Sample spacing");
    const StackGround ground(*this, layers_.size(), cache_.get(), threads);
    ground.HeightsAt(points, heights, spacing);
}

CraterFeatures Terrain::Craters(const RasterGrid& grid, double spacing) const {
    const double resolved = ListedSpacing(grid, spacing, surface_);
    CraterFeatures placed;
    ForEachLayer<CraterPlacer>(*this, [&](const CraterPlacer& placer, std::size_t) {
        placed.Append(placer.CratersOver(grid, resolved));
    });
    return placed;
}

BoulderFeatures Terrain::Boulders(const RasterGrid& grid, double spacing) const {
    const double resolved = ListedSpacing(grid, spacing, surface_);
    BoulderFeatures placed;
    ForEachLayer<BoulderPlacer>(*this, [&](const BoulderPlacer& placer, std::size_t n) {
        const StackGround ground(*this, n, cache_.get(), 0);
        placed.Append(placer.BouldersOver(grid, resolved, &ground));
    });
    return placed;
}

Terrain Terrain::Ground(std::size_t stages) const {
    if (stages > layers_.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(layers_.size()) +
                                    " layers, so it has no ground beneath layer " +
                                    std::to_string(stages));
    }
    Terrain prefix = *this;
    prefix.layers_.resize(stages);
    prefix.layerVersions_.resize(stages);
    return prefix;
}

void Terrain::SetLayer(std::size_t index, LayerPtr layer) {
    if (index >= layers_.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(layers_.size()) +
                                    " layers, so it has no layer " + std::to_string(index));
    }
    Terrain updated = *this;
    LayerReplacements replaced{{layers_[index].get(), layer}};
    std::vector<std::size_t> changed{index};
    updated.layers_[index] = std::move(layer);
    RebindFrom(index + 1, updated.layers_, replaced, changed);
    updated.Commit(false, changed);
    *this = std::move(updated);
}

void Terrain::SetSource(std::shared_ptr<const terraingen::Source> source) {
    Terrain updated = *this;
    updated.source_ = std::move(source);
    updated.Commit(true, {});
    *this = std::move(updated);
}

std::uint64_t Terrain::LayerVersion(std::size_t index) const {
    return layerVersions_.at(index);
}

Terrain Terrain::Reseed(std::uint64_t seed) const {
    Terrain reseeded = *this;
    bool sourceChanged = false;
    if (source_) {
        if (auto source = source_->Reseeded(PartSeed(seed, 0))) {
            reseeded.source_ = std::move(source);
            sourceChanged = true;
        }
    }
    LayerReplacements replaced;
    std::vector<std::size_t> changed;
    for (std::size_t n = 0; n < layers_.size(); ++n) {
        LayerPtr layer = layers_[n]->Reseeded(PartSeed(seed, n + 1));
        if (!layer) {
            layer = layers_[n];
        }
        if (LayerPtr rebound = replaced.empty() ? nullptr : layer->Rebound(replaced)) {
            layer = std::move(rebound);
        }
        if (layer != layers_[n]) {
            replaced.emplace_back(layers_[n].get(), layer);
            reseeded.layers_[n] = std::move(layer);
            changed.push_back(n);
        }
    }
    reseeded.Commit(sourceChanged, changed);
    return reseeded;
}

}  // namespace terraingen
