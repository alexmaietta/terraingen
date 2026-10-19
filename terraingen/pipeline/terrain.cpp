#include "terraingen/pipeline/terrain.h"

#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/util/arithmetic.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it, which shares `cache`.
void EvaluateStack(const Terrain& terrain, std::size_t layers, std::span<const Vec2d> points,
                   std::span<float> heights, double spacing, EvaluationCache* cache) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = terrain.source ? terrain.source->HeightAt(points[i].x, points[i].y) : 0.0f;
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(terrain, n, cache);
        SampleBlock samples(points, heights);
        terrain.layers[n]->Apply(samples, LayerContext{spacing, 1, &ground, Backend::Cpu, cache});
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
std::vector<std::size_t> MortonOrder(std::span<const Vec2d> points) {
    Bounds2d bounds{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Vec2d& p : points) {
        bounds = {std::fmin(bounds.minX, p.x), std::fmin(bounds.minY, p.y),
                  std::fmax(bounds.maxX, p.x), std::fmax(bounds.maxY, p.y)};
    }
    std::vector<std::pair<std::uint32_t, std::size_t>> keyed(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        keyed[i] = {MortonKey(points[i], bounds), i};
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::size_t> order(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        order[i] = keyed[i].second;
    }
    return order;
}

/// `EvaluateStack` over `points` in batches on `threads` threads, the points taken in Morton order
/// so each batch covers a small area and prepares only the features there; the batches share
/// `shared`, or a cache of their own when it is null.
void EvaluateBatched(const Terrain& terrain, std::size_t layers, std::span<const Vec2d> points,
                     std::span<float> heights, double spacing, unsigned threads,
                     EvaluationCache* shared) {
    constexpr std::size_t kBatch = 256;
    if (points.size() <= kBatch) {
        EvaluateStack(terrain, layers, points, heights, spacing, shared);
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
        EvaluateStack(terrain, layers, std::span<const Vec2d>(sorted).subspan(begin, count),
                      std::span<float>(values).subspan(begin, count), spacing, cache);
    });
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[order[i]] = values[i];
    }
}

/// The ground beneath the first layers of a terrain it keeps.
class OwnedGround final : public Ground {
  public:
    OwnedGround(const Terrain& terrain, std::size_t layers)
        : terrain_(std::make_shared<const Terrain>(terrain)), ground_(*terrain_, layers) {}

    float HeightAt(double x, double y, double spacing) const override {
        return ground_.HeightAt(x, y, spacing);
    }
    void HeightsAt(std::span<const Vec2d> points, std::span<float> heights,
                   double spacing) const override {
        ground_.HeightsAt(points, heights, spacing);
    }

  private:
    std::shared_ptr<const Terrain> terrain_;
    TerrainGround ground_;
};

}  // namespace

Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return Evaluate(terrain, grid, EvaluationOptions{spacing});
}

Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid,
                       const EvaluationOptions& options) {
    ValidateTerrain(terrain);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved = ResolvedSpacing(grid, options.spacing, terrain.surface);
    const Backend backend = ResolveBackend(options.backend.value_or(DefaultBackend()));
    Raster<float> heights = grid.MakeRaster();
    if (terrain.source) {
        terrain.source->Fill(heights);
    }
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const Stopwatch stopwatch;
        EvaluationCache cache;
        const TerrainGround ground(terrain, n, &cache);
        terrain.layers[n]->Apply(heights,
                                 LayerContext{resolved, options.threads, &ground, backend});
        if (options.onLayer) {
            options.onLayer(n, stopwatch.Milliseconds());
        }
    }
    return heights;
}

void ValidateTerrain(const Terrain& terrain) {
    if (std::any_of(terrain.layers.begin(), terrain.layers.end(),
                    [](const auto& l) { return !l; })) {
        throw std::invalid_argument("A terrain's layers must not be null");
    }
}

std::vector<float> EvaluatePoints(const Terrain& terrain, std::span<const Vec2d> points,
                                  double spacing, unsigned threads) {
    ValidateTerrain(terrain);
    RequirePositive(spacing, "Sample spacing");
    std::vector<float> heights(points.size());
    EvaluateBatched(terrain, terrain.layers.size(), points, heights, spacing, threads, nullptr);
    return heights;
}

float EvaluateAt(const Terrain& terrain, double x, double y, double spacing) {
    const Vec2d point{x, y};
    return EvaluatePoints(terrain, {&point, 1}, spacing)[0];
}

TerrainGround::TerrainGround(const Terrain& terrain, std::size_t layers, EvaluationCache* cache)
    : terrain_(terrain), layers_(layers), cache_(cache) {
    if (layers_ > terrain_.layers.size()) {
        throw std::invalid_argument("The terrain has fewer layers than the ground asks for");
    }
}

float TerrainGround::HeightAt(double x, double y, double spacing) const {
    const Vec2d point{x, y};
    float height = 0.0f;
    EvaluateStack(terrain_, layers_, {&point, 1}, {&height, 1}, spacing, cache_);
    return height;
}

void TerrainGround::HeightsAt(std::span<const Vec2d> points, std::span<float> heights,
                              double spacing) const {
    EvaluateBatched(terrain_, layers_, points, heights, spacing, 0, cache_);
}

std::shared_ptr<const Ground> SharedGround(const Terrain& terrain, std::size_t layers) {
    return std::make_shared<OwnedGround>(terrain, layers);
}

Raster<float> Evaluate(const Terrain& terrain) {
    ValidateTerrain(terrain);
    if (!terrain.source) {
        throw std::invalid_argument("A terrain without a source needs a grid to evaluate over");
    }
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
