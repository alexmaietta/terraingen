#include "terraingen/pipeline/terrain.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it.
void EvaluateStack(const Terrain& terrain, std::size_t layers, std::span<const Vec2d> points,
                   std::span<float> heights, double spacing) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = terrain.source ? terrain.source->HeightAt(points[i].x, points[i].y) : 0.0f;
    }
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(terrain, n);
        SampleBlock samples(points, heights);
        terrain.layers[n]->Apply(samples, LayerContext{spacing, 1, &ground});
    }
}

}  // namespace

Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return Evaluate(terrain, grid, EvaluationOptions{spacing, 1});
}

Raster<float> Evaluate(const Terrain& terrain, const RasterGrid& grid,
                       const EvaluationOptions& options) {
    ValidateTerrain(terrain);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved = std::max(options.spacing, grid.Spacing(terrain.surface));
    Raster<float> heights = grid.MakeRaster();
    if (terrain.source) {
        terrain.source->Fill(heights);
    }
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const TerrainGround ground(terrain, n);
        terrain.layers[n]->Apply(heights, LayerContext{resolved, options.threads, &ground});
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
    constexpr std::size_t kBatch = 256;
    std::vector<float> heights(points.size());
    ParallelFor((points.size() + kBatch - 1) / kBatch, threads, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(terrain, terrain.layers.size(), points.subspan(begin, count),
                      std::span<float>(heights).subspan(begin, count), spacing);
    });
    return heights;
}

float EvaluateAt(const Terrain& terrain, double x, double y, double spacing) {
    const Vec2d point{x, y};
    return EvaluatePoints(terrain, {&point, 1}, spacing, 1)[0];
}

TerrainGround::TerrainGround(const Terrain& terrain, std::size_t layers)
    : terrain_(terrain), layers_(layers) {
    if (layers_ > terrain_.layers.size()) {
        throw std::invalid_argument("The terrain has fewer layers than the ground asks for");
    }
}

float TerrainGround::HeightAt(double x, double y, double spacing) const {
    const Vec2d point{x, y};
    float height = 0.0f;
    EvaluateStack(terrain_, layers_, {&point, 1}, {&height, 1}, spacing);
    return height;
}

void TerrainGround::HeightsAt(std::span<const Vec2d> points, std::span<float> heights,
                              double spacing) const {
    constexpr std::size_t kBatch = 256;
    ParallelFor((points.size() + kBatch - 1) / kBatch, 0, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(terrain_, layers_, points.subspan(begin, count),
                      heights.subspan(begin, count), spacing);
    });
}

Raster<float> Evaluate(const Terrain& terrain) {
    ValidateTerrain(terrain);
    if (!terrain.source) {
        throw std::invalid_argument("A terrain without a source needs a grid to evaluate over");
    }
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
