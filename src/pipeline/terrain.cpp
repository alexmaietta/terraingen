// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/terrain.h"

#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

/// Points evaluated together, sharing the features they prepare.
constexpr std::size_t kBatch = 256;

/// The side in cells of the tiles a grid read of the ground is evaluated in, `kBatch` cells each.
constexpr std::size_t kTileSide = 16;

/// The 64-bit seed of part `part` of a terrain reseeded with `seed`: 0 the source, n + 1 layer n.
std::uint64_t PartSeed(std::uint64_t seed, std::size_t part) {
    const auto word = static_cast<std::uint32_t>(2 * part);
    return JoinWords(ReseedWord(seed, word), ReseedWord(seed, word + 1));
}

/// The sample spacing in meters `options` evaluate `grid` at on `surface`.
/// @throws std::invalid_argument for a negative or non-finite spacing.
double EvaluatedSpacing(const RasterGrid& grid, const EvaluationOptions& options,
                        const Surface& surface) {
    RequireNonNegative(options.spacing, "Sample spacing");
    return ResolvedSpacing(grid, options.spacing, surface);
}

void ReadStack(const Terrain& terrain, std::size_t layers, SampleBlock& samples, double spacing,
               unsigned threads, EvaluationCache* cache);

/// The ground beneath layer `layers` of a terrain during one evaluation, whose reads share
/// `cache`.
class StackGround final : public Ground {
  public:
    StackGround(const Terrain& terrain, std::size_t layers, EvaluationCache* cache)
        : m_terrain(terrain), m_layers(layers), m_cache(cache) {}

    LayerContext Context(double spacing, unsigned threads) const override {
        LayerContext context = m_terrain.Context(spacing, threads);
        context.ground = this;
        return context;
    }

  protected:
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override {
        ReadStack(m_terrain, m_layers, samples, spacing, threads, m_cache);
    }

  private:
    const Terrain& m_terrain;
    std::size_t m_layers;
    EvaluationCache* m_cache;
};

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it, which shares `cache`.
void EvaluateStack(const Terrain& terrain, std::size_t layers, Span<const Vec2d> points,
                   Span<float> heights, double spacing, EvaluationCache* cache) {
    if (terrain.Source()) {
        terrain.Source()->HeightsAt(points, heights, spacing / terrain.Surface().UnitLength());
    } else {
        std::fill(heights.begin(), heights.end(), 0.0f);
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const StackGround ground(terrain, n, cache);
        SampleBlock samples(points, heights);
        LayerContext context = ground.Context(spacing, 1);
        context.cache = cache;
        terrain.Layers()[n]->Apply(samples, context);
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

/// The indices of `points`, fewer than 2^32, sorted by their Morton codes over the points'
/// bounds, ties by index.
std::vector<std::size_t> MortonOrder(Span<const Vec2d> points) {
    Bounds2d bounds{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Vec2d& p : points) {
        bounds = {std::fmin(bounds.minX, p.x), std::fmin(bounds.minY, p.y),
                  std::fmax(bounds.maxX, p.x), std::fmax(bounds.maxY, p.y)};
    }
    std::vector<std::uint64_t> keyed(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        keyed[i] = (std::uint64_t{MortonKey(points[i], bounds)} << 32U) | std::uint64_t{i};
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::size_t> order(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        order[i] = static_cast<std::size_t>(keyed[i] & 0xFFFFFFFFU);
    }
    return order;
}

/// `EvaluateStack` over `points` in batches on `threads` threads, the points taken in Morton order
/// so each batch covers a small area and prepares only the features there.
void EvaluatePoints(const Terrain& terrain, std::size_t layers, Span<const Vec2d> points,
                    Span<float> heights, double spacing, unsigned threads, EvaluationCache* cache) {
    if (points.size() <= kBatch) {
        EvaluateStack(terrain, layers, points, heights, spacing, cache);
        return;
    }
    const std::vector<std::size_t> order = MortonOrder(points);
    std::vector<Vec2d> sorted(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        sorted[i] = points[order[i]];
    }
    std::vector<float> values(points.size());
    ParallelFor(CeilDiv(points.size(), kBatch), threads, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(terrain, layers, Span<const Vec2d>(sorted).subspan(begin, count),
                      Span<float>(values).subspan(begin, count), spacing, cache);
    });
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[order[i]] = values[i];
    }
}

/// `EvaluateStack` over the cells of the grid block `samples` in tiles of `kTileSide` squared
/// cells, which are already near each other, on `threads` threads.
void EvaluateTiles(const Terrain& terrain, std::size_t layers, const SampleBlock& samples,
                   double spacing, unsigned threads, EvaluationCache* cache) {
    const std::size_t width = samples.Grid()->Width();
    const std::size_t height = samples.Grid()->Height();
    const std::size_t across = CeilDiv(width, kTileSide);
    ParallelFor(across * CeilDiv(height, kTileSide), threads, [&](std::size_t tile) {
        const std::size_t col = (tile % across) * kTileSide;
        const std::size_t row = (tile / across) * kTileSide;
        const std::size_t cols = std::min(kTileSide, width - col);
        const std::size_t rows = std::min(kTileSide, height - row);
        std::vector<Vec2d> points(cols * rows);
        for (std::size_t k = 0; k < points.size(); ++k) {
            points[k] = samples.Position((row + k / cols) * width + col + k % cols);
        }
        std::vector<float> values(points.size());
        EvaluateStack(terrain, layers, points, values, spacing, cache);
        for (std::size_t k = 0; k < points.size(); ++k) {
            samples.Height((row + k / cols) * width + col + k % cols) = values[k];
        }
    });
}

/// Sets `samples` to the terrain's source and first `layers` layers on `threads` threads; the
/// reads share `cache`, or one of their own when it is null.
void ReadStack(const Terrain& terrain, std::size_t layers, SampleBlock& samples, double spacing,
               unsigned threads, EvaluationCache* cache) {
    std::optional<EvaluationCache> own;
    EvaluationCache* shared = cache != nullptr ? cache : &own.emplace();
    if (samples.IsGrid()) {
        EvaluateTiles(terrain, layers, samples, spacing, threads, shared);
    } else {
        EvaluatePoints(terrain, layers, samples.Points(), samples.Heights(), spacing, threads,
                       shared);
    }
}

/// Checks that `source` can be the source of a terrain on `surface`.
void ValidateSource(const Surface& surface, const Source* source) {
    if (source == nullptr) {
        return;
    }
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

/// Checks that `layers` on `surface` over `source` can be evaluated.
void Validate(const Surface& surface, const std::shared_ptr<const Source>& source,
              const std::vector<LayerPtr>& layers) {
    ValidateSource(surface, source.get());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (!layers[i]) {
            throw std::invalid_argument("A terrain's layers must not be null");
        }
        const std::optional<TerrainBase> base = layers[i]->LayerBase();
        if (base && !(base->surface == surface)) {
            throw std::invalid_argument("Layer " + std::to_string(i) +
                                        " was built on another surface than the terrain's");
        }
        if (base && base->source && base->source != source) {
            throw std::invalid_argument("Layer " + std::to_string(i) +
                                        " was built on another source than the terrain's");
        }
        const auto before = layers.begin() + static_cast<std::ptrdiff_t>(i);
        for (const Layer* dependency : layers[i]->Dependencies()) {
            if (std::none_of(layers.begin(), before,
                             [dependency](const LayerPtr& l) { return l.get() == dependency; })) {
                throw std::invalid_argument(
                    "Layer " + std::to_string(i) +
                    " reads a layer that does not come before it in the terrain's layers");
            }
        }
    }
}

}  // namespace

Terrain::Terrain() : Terrain(terraingen::Surface{}, nullptr, {}) {}

Terrain::Terrain(terraingen::Surface surface, std::shared_ptr<const terraingen::Source> source,
                 std::vector<LayerPtr> layers)
    : m_surface(std::move(surface)), m_source(std::move(source)), m_layers(std::move(layers)),
      m_cache(std::make_shared<EvaluationCache>(kPointQueryValues)) {
    Validate(m_surface, m_source, m_layers);
}

std::optional<RasterGrid> Terrain::Grid() const {
    return m_source ? BoundedGrid(*m_source) : std::nullopt;
}

RasterGrid Terrain::SourceGrid() const {
    const std::optional<RasterGrid> grid = Grid();
    if (!grid) {
        throw std::invalid_argument(
            "A terrain without a source, or with a source without bounds such as synthetic "
            "noise, needs a grid");
    }
    return *grid;
}

GeoKeys Terrain::Keys() const {
    GeoKeys keys = m_source ? m_source->Keys() : GeoKeys{};
    if (m_surface.IsBody() && keys.Empty()) {
        keys = GeographicSphereKeys(m_surface.Radius());
    }
    return keys;
}

LayerContext Terrain::Context(double spacing, unsigned threads) const {
    LayerContext context = Ground::Context(spacing, threads);
    context.surface = &m_surface;
    context.source = m_source.get();
    return context;
}

Raster<float> Terrain::Evaluate(const RasterGrid& grid, const EvaluationOptions& options) const {
    Raster<float> heights(grid);
    Fill(grid, heights.Values(), options);
    return heights;
}

Raster<float> Terrain::Evaluate(const EvaluationOptions& options) const {
    return Evaluate(SourceGrid(), options);
}

void Terrain::Fill(const RasterGrid& grid, Span<float> heights,
                   const EvaluationOptions& options) const {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("The grid needs " + std::to_string(grid.width * grid.height) +
                                    " heights, not " + std::to_string(heights.size()));
    }
    Raster<float> view(grid, heights);
    const double spacing = EvaluatedSpacing(grid, options, m_surface);
    if (m_source) {
        m_source->Fill(view, spacing / m_surface.UnitLength(), options.threads);
    } else {
        std::fill(heights.begin(), heights.end(), 0.0f);
    }
    ApplyFrom(0, view, options);
}

void Terrain::ApplyFrom(std::size_t first, Raster<float>& heights,
                        const EvaluationOptions& options) const {
    if (first > m_layers.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(m_layers.size()) +
                                    " layers, so it has none from layer " + std::to_string(first));
    }
    const double spacing = EvaluatedSpacing(heights.Grid(), options, m_surface);
    const Backend backend = ResolveBackend(options.backend.value_or(DefaultBackend()));
    for (std::size_t n = first; n < m_layers.size(); ++n) {
        const Stopwatch stopwatch;
        EvaluationCache cache;
        const StackGround ground(*this, n, &cache);
        LayerContext context = ground.Context(spacing, options.threads);
        context.backend = backend;
        m_layers[n]->Apply(heights, context);
        if (options.onLayer) {
            options.onLayer(n, stopwatch.Milliseconds());
        }
    }
}

void Terrain::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    RequirePositive(spacing, "Sample spacing");
    ReadStack(*this, m_layers.size(), samples, spacing, threads, m_cache.get());
}

CraterFeatures Terrain::Craters(const RasterGrid& grid, double spacing) const {
    const double resolved = ListedSpacing(grid, spacing, m_surface);
    CraterFeatures placed;
    ForEachLayer<CraterPlacer>(*this, [&](const CraterPlacer& placer, std::size_t) {
        placed.Append(placer.CratersOver(grid, resolved));
    });
    return placed;
}

CraterFeatures Terrain::Craters(double spacing) const {
    return Craters(SourceGrid(), spacing);
}

BoulderFeatures Terrain::Boulders(const RasterGrid& grid, double spacing) const {
    const double resolved = ListedSpacing(grid, spacing, m_surface);
    BoulderFeatures placed;
    ForEachLayer<BoulderPlacer>(*this, [&](const BoulderPlacer& placer, std::size_t n) {
        const StackGround ground(*this, n, m_cache.get());
        placed.Append(placer.BouldersOver(grid, resolved, &ground));
    });
    return placed;
}

BoulderFeatures Terrain::Boulders(double spacing) const {
    return Boulders(SourceGrid(), spacing);
}

Terrain Terrain::Beneath(std::size_t layers) const {
    if (layers > m_layers.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(m_layers.size()) +
                                    " layers, so it has no ground beneath layer " +
                                    std::to_string(layers));
    }
    Terrain prefix = *this;
    prefix.m_layers.resize(layers);
    return prefix;
}

void Terrain::Replace(std::shared_ptr<const terraingen::Source> source,
                      const LayerReplacer& replace) {
    std::vector<LayerPtr> layers = m_layers;
    const TerrainBase base{m_surface, source};
    LayerReplacements replaced;
    for (std::size_t n = 0; n < layers.size(); ++n) {
        LayerPtr layer = replace(n, layers[n]);
        layer = layer ? std::move(layer) : layers[n];
        const std::optional<TerrainBase> built = layer->LayerBase();
        if (source != m_source && built && built->source == m_source) {
            if (LayerPtr rebased = layer->Rebased(base)) {
                layer = std::move(rebased);
            }
        }
        if (LayerPtr rebound = replaced.empty() ? nullptr : layer->Rebound(replaced)) {
            layer = std::move(rebound);
        }
        if (layer != layers[n]) {
            replaced.emplace_back(layers[n].get(), layer);
            layers[n] = std::move(layer);
        }
    }
    Validate(m_surface, source, layers);
    m_source = std::move(source);
    m_layers = std::move(layers);
    m_cache = std::make_shared<EvaluationCache>(kPointQueryValues);
}

void Terrain::SetLayer(std::size_t index, LayerPtr layer) {
    if (index >= m_layers.size()) {
        throw std::invalid_argument("The terrain has " + std::to_string(m_layers.size()) +
                                    " layers, so it has no layer " + std::to_string(index));
    }
    if (!layer) {
        throw std::invalid_argument("A terrain's layers must not be null");
    }
    Replace(m_source, [&](std::size_t n, const LayerPtr&) { return n == index ? layer : nullptr; });
}

void Terrain::SetSource(std::shared_ptr<const terraingen::Source> source) {
    Replace(std::move(source), [](std::size_t, const LayerPtr&) { return nullptr; });
}

Terrain Terrain::Reseed(std::uint64_t seed) const {
    Terrain reseeded = *this;
    std::shared_ptr<const terraingen::Source> source =
        m_source ? m_source->Reseeded(PartSeed(seed, 0)) : nullptr;
    reseeded.Replace(source ? std::move(source) : m_source,
                     [seed](std::size_t n, const LayerPtr& layer) {
                         return layer->Reseeded(PartSeed(seed, n + 1));
                     });
    return reseeded;
}

}  // namespace terraingen
