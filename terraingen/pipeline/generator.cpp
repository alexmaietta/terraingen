#include "terraingen/pipeline/generator.h"

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

/// Sets `heights` to the generator's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it, which shares `cache`.
void EvaluateStack(const TerrainGenerator& generator, std::size_t layers, Span<const Vec2d> points,
                   Span<float> heights, double spacing, EvaluationCache* cache) {
    const double sourceSpacing = spacing / generator.surface.UnitLength();
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = generator.source
                         ? generator.source->HeightAt(points[i].x, points[i].y, sourceSpacing)
                         : 0.0f;
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(generator, n, cache);
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
void EvaluateBatched(const TerrainGenerator& generator, std::size_t layers,
                     Span<const Vec2d> points, Span<float> heights, double spacing,
                     unsigned threads, EvaluationCache* shared) {
    constexpr std::size_t kBatch = 256;
    if (points.size() <= kBatch) {
        EvaluateStack(generator, layers, points, heights, spacing, shared);
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
                      Span<float>(values).subspan(begin, count), spacing, cache);
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

Raster<float> Evaluate(const TerrainGenerator& generator, const RasterGrid& grid,
                       const EvaluationOptions& options) {
    ValidateGenerator(generator);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved = ResolvedSpacing(grid, options.spacing, generator.surface);
    const Backend backend = ResolveBackend(options.backend.value_or(DefaultBackend()));
    Raster<float> heights = grid.MakeRaster();
    if (generator.source) {
        generator.source->Fill(heights, resolved / generator.surface.UnitLength());
    }
    for (std::size_t n = 0; n < generator.layers.size(); ++n) {
        const Stopwatch stopwatch;
        EvaluationCache cache;
        const TerrainGround ground(generator, n, &cache);
        generator.layers[n]->Apply(heights,
                                   LayerContext{resolved, options.threads, &ground, backend});
        if (options.onLayer) {
            options.onLayer(n, stopwatch.Milliseconds());
        }
    }
    return heights;
}

void FillHeights(const TerrainGenerator& generator, const RasterGrid& grid, Span<float> heights,
                 const EvaluationOptions& options) {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("The grid needs " + std::to_string(grid.width * grid.height) +
                                    " heights, not " + std::to_string(heights.size()));
    }
    const Raster<float> evaluated = Evaluate(generator, grid, options);
    const Span<const float> values = evaluated.Values();
    std::copy(values.begin(), values.end(), heights.begin());
}

void ValidateGenerator(const TerrainGenerator& generator) {
    if (std::any_of(generator.layers.begin(), generator.layers.end(),
                    [](const auto& l) { return !l; })) {
        throw std::invalid_argument("A terrain's layers must not be null");
    }
}

std::vector<float> HeightsAt(const TerrainGenerator& generator, Span<const Vec2d> points,
                             double spacing, unsigned threads) {
    ValidateGenerator(generator);
    RequirePositive(spacing, "Sample spacing");
    std::vector<float> heights(points.size());
    EvaluateBatched(generator, generator.layers.size(), points, heights, spacing, threads, nullptr);
    return heights;
}

float HeightAt(const TerrainGenerator& generator, double x, double y, double spacing) {
    const Vec2d point{x, y};
    return HeightsAt(generator, {&point, 1}, spacing)[0];
}

TerrainGround::TerrainGround(const TerrainGenerator& generator, std::size_t layers,
                             EvaluationCache* cache)
    : generator_(generator), layers_(layers), cache_(cache) {
    if (layers_ > generator_.layers.size()) {
        throw std::invalid_argument("The terrain has fewer layers than the ground asks for");
    }
}

float TerrainGround::HeightAt(double x, double y, double spacing) const {
    const Vec2d point{x, y};
    float height = 0.0f;
    EvaluateStack(generator_, layers_, {&point, 1}, {&height, 1}, spacing, cache_);
    return height;
}

void TerrainGround::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    EvaluateBatched(generator_, layers_, points, heights, spacing, 0, cache_);
}

std::shared_ptr<const Ground> SharedGround(const TerrainGenerator& generator, std::size_t layers) {
    return std::make_shared<OwnedGround>(generator, layers);
}

Raster<float> Evaluate(const TerrainGenerator& generator) {
    ValidateGenerator(generator);
    const std::optional<RasterGrid> grid =
        generator.source ? BoundedGrid(*generator.source) : std::nullopt;
    if (!grid) {
        throw std::invalid_argument(
            "A terrain without a source, or with a source without bounds such as synthetic "
            "noise, needs a grid to evaluate over");
    }
    return Evaluate(generator, *grid);
}

CraterFeatures PlacedCraters(const TerrainGenerator& generator, const RasterGrid& grid,
                             double spacing) {
    return PlacedCraters(generator.layers, grid, ListedSpacing(grid, spacing, generator.surface));
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
