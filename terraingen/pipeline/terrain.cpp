/// @file
/// @brief Evaluating a terrain over grids and at points.
#include "terraingen/pipeline/terrain.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

void CheckTerrain(const Terrain& terrain) {
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    if (std::any_of(terrain.layers.begin(), terrain.layers.end(),
                    [](const auto& l) { return !l; }) ||
        std::any_of(terrain.filters.begin(), terrain.filters.end(),
                    [](const auto& f) { return !f; })) {
        throw std::invalid_argument("A terrain's layers and filters must not be null");
    }
}

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it.
void EvaluateStack(const Terrain& terrain, std::size_t layers, std::span<const Vec2d> points,
                   std::span<float> heights, double spacing) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = terrain.source->HeightAt(points[i].x, points[i].y);
    }
    TerrainFeatures features;
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(terrain, n);
        SampleBlock samples(points, heights);
        terrain.layers[n]->Apply(samples, LayerContext{spacing, 1, &ground}, features);
    }
}

}  // namespace

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return Evaluate(terrain, grid, EvaluationOptions{spacing, 1});
}

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid,
                          const EvaluationOptions& options) {
    CheckTerrain(terrain);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved = std::max(options.spacing, grid.Spacing(terrain.surface));
    EvaluatedTerrain result{grid.MakeRaster(), {}};
    terrain.source->Fill(result.heights);
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const TerrainGround ground(terrain, n);
        terrain.layers[n]->Apply(result.heights, LayerContext{resolved, options.threads, &ground},
                                 result.features);
    }
    for (const auto& filter : terrain.filters) {
        filter->Apply(result.heights, {resolved, options.threads}, result.features);
    }
    return result;
}

double FilterMargin(const Terrain& terrain, double spacing) {
    CheckTerrain(terrain);
    double largest = 0.0;
    for (const auto& layer : terrain.layers) {
        largest = std::fmax(largest, layer->MaxFeatureDiameter());
    }
    double margin = 0.0;
    for (const auto& filter : terrain.filters) {
        margin = std::fmax(margin, filter->Margin(largest, spacing));
    }
    return margin;
}

std::vector<float> EvaluatePoints(const Terrain& terrain, std::span<const Vec2d> points,
                                  double spacing, unsigned threads) {
    CheckTerrain(terrain);
    if (!terrain.filters.empty()) {
        throw std::invalid_argument(
            "Point queries cannot run filters, which read neighbouring cells; evaluate a grid");
    }
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
    if (!terrain_.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
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

EvaluatedTerrain Evaluate(const Terrain& terrain) {
    CheckTerrain(terrain);
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
