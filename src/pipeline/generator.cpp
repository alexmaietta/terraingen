// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/generator.h"

#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Generators whose point queries keep a default cache, and values each cache holds.
constexpr std::size_t kCachedGenerators = 4;
constexpr std::size_t kDefaultCacheValues = std::size_t{1} << 17U;

/// A generator as its default cache knows it: its surface and weak references to its parts, so it
/// matches only while every part is the same live object, and keeps none of them alive.
struct GeneratorKey {
    Surface surface;
    std::weak_ptr<const Source> source;
    const Source* sourceAddress = nullptr;
    std::vector<std::weak_ptr<const Layer>> layers;
    std::vector<const Layer*> layerAddresses;

    explicit GeneratorKey(const TerrainGenerator& generator)
        : surface(generator.surface), source(generator.source),
          sourceAddress(generator.source.get()) {
        for (const std::shared_ptr<const Layer>& layer : generator.layers) {
            layers.push_back(layer);
            layerAddresses.push_back(layer.get());
        }
    }

    bool Matches(const TerrainGenerator& generator) const {
        if (!(surface == generator.surface) || generator.source.get() != sourceAddress ||
            source.lock().get() != sourceAddress ||
            generator.layers.size() != layerAddresses.size()) {
            return false;
        }
        for (std::size_t n = 0; n < layers.size(); ++n) {
            if (generator.layers[n].get() != layerAddresses[n] ||
                layers[n].lock().get() != layerAddresses[n]) {
                return false;
            }
        }
        return true;
    }

    bool Expired() const {
        if (sourceAddress != nullptr && source.expired()) {
            return true;
        }
        return std::any_of(layers.begin(), layers.end(),
                           [](const std::weak_ptr<const Layer>& layer) { return layer.expired(); });
    }
};

/// The cache point queries of `generator` share when the caller gives none: one per recently
/// queried generator, dropped once one of its parts is freed or it is the least recently used.
/// A generator is validated (`ValidateGenerator`) when its cache is made.
std::shared_ptr<EvaluationCache> DefaultCache(const TerrainGenerator& generator) {
    static std::mutex mutex;
    static std::list<std::pair<GeneratorKey, std::shared_ptr<EvaluationCache>>> caches;
    const std::lock_guard lock(mutex);
    caches.remove_if([](const auto& entry) { return entry.first.Expired(); });
    for (auto entry = caches.begin(); entry != caches.end(); ++entry) {
        if (entry->first.Matches(generator)) {
            caches.splice(caches.begin(), caches, entry);
            return caches.front().second;
        }
    }
    ValidateGenerator(generator);
    caches.emplace_front(GeneratorKey(generator),
                         std::make_shared<EvaluationCache>(kDefaultCacheValues));
    if (caches.size() > kCachedGenerators) {
        caches.pop_back();
    }
    return caches.front().second;
}

/// Sets `heights` to the generator's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it, which shares `cache` and reads in batches over
/// `threads` threads.
void EvaluateStack(const TerrainGenerator& generator, std::size_t layers, Span<const Vec2d> points,
                   Span<float> heights, double spacing, EvaluationCache* cache, unsigned threads) {
    const double sourceSpacing = spacing / generator.surface.UnitLength();
    if (generator.source) {
        generator.source->HeightsAt(points, heights, sourceSpacing);
    } else {
        std::fill(heights.begin(), heights.end(), 0.0f);
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(generator, n, cache, threads);
        SampleBlock samples(points, heights);
        generator.layers[n]->Apply(samples, LayerContext{spacing, 1, &ground, Backend::Cpu, cache});
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
/// `shared`, or a cache of their own when it is null.
void EvaluateBatched(const TerrainGenerator& generator, std::size_t layers,
                     Span<const Vec2d> points, Span<float> heights, double spacing,
                     unsigned threads, EvaluationCache* shared) {
    constexpr std::size_t kBatch = 256;
    if (points.size() <= kBatch) {
        EvaluateStack(generator, layers, points, heights, spacing, shared, threads);
        return;
    }
    const std::vector<std::size_t> order = MortonOrder(points);
    std::vector<Vec2d> sorted(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        sorted[i] = points[order[i]];
    }
    std::vector<float> values(points.size());
    std::optional<EvaluationCache> own;
    EvaluationCache* cache = shared != nullptr ? shared : &own.emplace();
    ParallelFor(CeilDiv(points.size(), kBatch), threads, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(generator, layers, Span<const Vec2d>(sorted).subspan(begin, count),
                      Span<float>(values).subspan(begin, count), spacing, cache, threads);
    });
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[order[i]] = values[i];
    }
}

/// The ground beneath the first layers of a generator it keeps.
class OwnedGround final : public Ground {
  public:
    OwnedGround(const TerrainGenerator& generator, std::size_t layers)
        : generator_(std::make_shared<const TerrainGenerator>(generator)),
          ground_(*generator_, layers) {}

    float HeightAt(double x, double y, double spacing) const override {
        return ground_.HeightAt(x, y, spacing);
    }
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const override {
        ground_.HeightsAt(points, heights, spacing);
    }

  private:
    std::shared_ptr<const TerrainGenerator> generator_;
    TerrainGround ground_;
};

}  // namespace

Raster<float> Evaluate(const TerrainGenerator& generator, const RasterGrid& grid, double spacing) {
    return Evaluate(generator, grid, EvaluationOptions{spacing});
}

namespace {

/// Applies the layers of a validated `generator` from index `first` onward to `heights` at the
/// resolved sample spacing `resolved` meters.
void ApplyLayersAt(const TerrainGenerator& generator, std::size_t first, Raster<float>& heights,
                   double resolved, const EvaluationOptions& options) {
    const Backend backend = ResolveBackend(options.backend.value_or(DefaultBackend()));
    for (std::size_t n = first; n < generator.layers.size(); ++n) {
        const Stopwatch stopwatch;
        EvaluationCache cache;
        const TerrainGround ground(generator, n, &cache, options.threads);
        generator.layers[n]->Apply(heights,
                                   LayerContext{resolved, options.threads, &ground, backend});
        if (options.onLayer) {
            options.onLayer(n, stopwatch.Milliseconds());
        }
    }
}

/// Evaluates `generator` into every cell of `heights`, placed by its transform: the source, or
/// the datum without one, then each layer.
void EvaluateInto(const TerrainGenerator& generator, Raster<float>& heights,
                  const EvaluationOptions& options) {
    ValidateGenerator(generator);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved =
        ResolvedSpacing(RasterGrid::FromRaster(heights), options.spacing, generator.surface);
    if (generator.source) {
        generator.source->Fill(heights, resolved / generator.surface.UnitLength(), options.threads);
    } else {
        std::fill(heights.Values().begin(), heights.Values().end(), 0.0f);
    }
    ApplyLayersAt(generator, 0, heights, resolved, options);
}

}  // namespace

Raster<float> Evaluate(const TerrainGenerator& generator, const RasterGrid& grid,
                       const EvaluationOptions& options) {
    Raster<float> heights = grid.MakeRaster();
    EvaluateInto(generator, heights, options);
    return heights;
}

void ApplyLayers(const TerrainGenerator& generator, std::size_t first, Raster<float>& heights,
                 const EvaluationOptions& options) {
    ValidateGenerator(generator);
    RequireNonNegative(options.spacing, "Sample spacing");
    if (first > generator.layers.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(generator.layers.size()) +
                                    " layers, so it cannot apply them from layer " +
                                    std::to_string(first));
    }
    const double resolved =
        ResolvedSpacing(RasterGrid::FromRaster(heights), options.spacing, generator.surface);
    ApplyLayersAt(generator, first, heights, resolved, options);
}

void FillHeights(const TerrainGenerator& generator, const RasterGrid& grid, Span<float> heights,
                 const EvaluationOptions& options) {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("The grid needs " + std::to_string(grid.width * grid.height) +
                                    " heights, not " + std::to_string(heights.size()));
    }
    Raster<float> view(grid.width, grid.height, heights);
    view.SetTransform(grid.transform);
    EvaluateInto(generator, view, options);
}

void ValidateGenerator(const TerrainGenerator& generator) {
    if (std::any_of(generator.layers.begin(), generator.layers.end(),
                    [](const auto& l) { return !l; })) {
        throw std::invalid_argument("A terrain's layers must not be null");
    }
    if (generator.source) {
        const GeoKeys keys = generator.source->Keys();
        if (keys.IsGeographic() && !generator.surface.IsBody()) {
            throw std::invalid_argument(
                "The source is in longitude and latitude (a geographic CRS); craters are sized in "
                "meters, so reproject it first, for example with gdalwarp -t_srs, or set the "
                "terrain's surface to a sphere or ellipsoid");
        }
        if (generator.surface.IsBody() && !keys.Empty() && !keys.IsGeographic()) {
            throw std::invalid_argument(
                "On a body the source must be in longitude and latitude (a geographic CRS)");
        }
    }
    for (std::size_t i = 0; i < generator.layers.size(); ++i) {
        const Layer& layer = *generator.layers[i];
        const std::optional<Surface> surface = layer.LayerSurface();
        if (surface && !(*surface == generator.surface)) {
            throw std::invalid_argument("Layer " + std::to_string(i) +
                                        " was built for another surface than the terrain's");
        }
        for (const Layer* dependency : layer.Dependencies()) {
            const auto before = generator.layers.begin() + static_cast<std::ptrdiff_t>(i);
            if (std::none_of(generator.layers.begin(), before,
                             [dependency](const auto& l) { return l.get() == dependency; })) {
                throw std::invalid_argument(
                    "Layer " + std::to_string(i) +
                    " reads a layer that does not come before it in the terrain's layers");
            }
        }
    }
}

GeoKeys TerrainGeoKeys(const TerrainGenerator& generator) {
    GeoKeys keys = generator.source ? generator.source->Keys() : GeoKeys{};
    if (generator.surface.IsBody() && keys.Empty()) {
        keys = GeographicSphereKeys(generator.surface.Radius());
    }
    return keys;
}

void WriteGeoTiff(const std::filesystem::path& path, const Raster<float>& raster,
                  const TerrainGenerator& generator) {
    WriteGeoTiff(path, raster, TerrainGeoKeys(generator));
}

namespace {

/// `HeightsAt` into `heights`, validating the generator once per default cache.
void FillHeightsAt(const TerrainGenerator& generator, Span<const Vec2d> points, Span<float> heights,
                   double spacing, unsigned threads, EvaluationCache* cache) {
    RequirePositive(spacing, "Sample spacing");
    std::shared_ptr<EvaluationCache> kept;
    if (cache == nullptr) {
        kept = DefaultCache(generator);
        cache = kept.get();
    } else {
        ValidateGenerator(generator);
    }
    EvaluateBatched(generator, generator.layers.size(), points, heights, spacing, threads, cache);
}

}  // namespace

std::vector<float> HeightsAt(const TerrainGenerator& generator, Span<const Vec2d> points,
                             double spacing, unsigned threads, EvaluationCache* cache) {
    std::vector<float> heights(points.size());
    FillHeightsAt(generator, points, heights, spacing, threads, cache);
    return heights;
}

float HeightAt(const TerrainGenerator& generator, double x, double y, double spacing,
               EvaluationCache* cache) {
    const Vec2d point{x, y};
    float height = 0.0f;
    FillHeightsAt(generator, {&point, 1}, {&height, 1}, spacing, 0, cache);
    return height;
}

TerrainGround::TerrainGround(const TerrainGenerator& generator, std::size_t layers,
                             EvaluationCache* cache, unsigned threads)
    : generator_(generator), layers_(layers), cache_(cache), threads_(threads) {
    if (layers_ > generator_.layers.size()) {
        throw std::invalid_argument("The terrain has fewer layers than the ground asks for");
    }
}

float TerrainGround::HeightAt(double x, double y, double spacing) const {
    const Vec2d point{x, y};
    float height = 0.0f;
    EvaluateStack(generator_, layers_, {&point, 1}, {&height, 1}, spacing, cache_, threads_);
    return height;
}

void TerrainGround::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    EvaluateBatched(generator_, layers_, points, heights, spacing, threads_, cache_);
}

std::shared_ptr<const Ground> SharedGround(const TerrainGenerator& generator, std::size_t layers) {
    return std::make_shared<OwnedGround>(generator, layers);
}

std::optional<RasterGrid> SourceGrid(const TerrainGenerator& generator) {
    return generator.source ? BoundedGrid(*generator.source) : std::nullopt;
}

Raster<float> Evaluate(const TerrainGenerator& generator) {
    ValidateGenerator(generator);
    const std::optional<RasterGrid> grid = SourceGrid(generator);
    if (!grid) {
        throw std::invalid_argument(
            "A terrain without a source, or with a source without bounds such as synthetic "
            "noise, needs a grid to evaluate over");
    }
    return Evaluate(generator, *grid);
}

CraterFeatures PlacedCraters(const TerrainGenerator& generator, const RasterGrid& grid,
                             double spacing) {
    const double resolved = ListedSpacing(grid, spacing, generator.surface);
    CraterFeatures placed;
    ForEachLayer<CraterPlacer>(generator, [&](const CraterPlacer& placer, std::size_t) {
        placed.Append(placer.CratersOver(grid, resolved));
    });
    return placed;
}

BoulderFeatures PlacedBoulders(const TerrainGenerator& generator, const RasterGrid& grid,
                               double spacing) {
    const double resolved = ListedSpacing(grid, spacing, generator.surface);
    BoulderFeatures placed;
    ForEachLayer<BoulderPlacer>(generator, [&](const BoulderPlacer& placer, std::size_t n) {
        const TerrainGround ground(generator, n);
        placed.Append(placer.BouldersOver(grid, resolved, &ground));
    });
    return placed;
}

}  // namespace terraingen
