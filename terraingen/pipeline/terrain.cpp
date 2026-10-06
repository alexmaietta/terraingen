#include "terraingen/pipeline/terrain.h"

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

}  // namespace

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return Evaluate(terrain, grid, EvaluationOptions{spacing, 1});
}

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid,
                          const EvaluationOptions& options) {
    CheckTerrain(terrain);
    if (!(std::isfinite(options.spacing) && options.spacing >= 0.0)) {
        throw std::invalid_argument("Sample spacing must be finite and not negative");
    }
    const double resolved = std::max(options.spacing, grid.PixelSize());
    EvaluatedTerrain result{grid.MakeRaster(), {}};
    terrain.source->Fill(result.heights);
    for (const auto& layer : terrain.layers) {
        layer->ApplyParallel(result.heights, resolved, result.features, options.threads);
    }
    for (const auto& filter : terrain.filters) {
        filter->ApplyParallel(result.heights, resolved, result.features, options.threads);
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

EvaluatedTerrain EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                              unsigned threads) {
    scheme.Validate();
    const RasterGrid tile = scheme.Grid(key);
    const double spacing = tile.PixelSize();
    const auto margin =
        static_cast<std::size_t>(std::ceil(FilterMargin(terrain, spacing) / spacing));
    if (margin == 0) {
        return Evaluate(terrain, tile, EvaluationOptions{spacing, threads});
    }
    const Vec2d corner =
        tile.transform.PixelToWorld(-static_cast<double>(margin), -static_cast<double>(margin));
    const RasterGrid padded{tile.width + 2 * margin, tile.height + 2 * margin,
                            GeoTransform::NorthUp(corner.x, corner.y, spacing)};
    EvaluatedTerrain evaluated = Evaluate(terrain, padded, EvaluationOptions{spacing, threads});
    EvaluatedTerrain result{tile.MakeRaster(), std::move(evaluated.features)};
    result.heights.SetNoData(evaluated.heights.NoData());
    for (std::size_t row = 0; row < tile.height; ++row) {
        for (std::size_t col = 0; col < tile.width; ++col) {
            result.heights(col, row) = evaluated.heights(col + margin, row + margin);
        }
    }
    return result;
}

std::vector<EvaluatedTerrain> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                            const std::vector<TileKey>& keys, unsigned threads) {
    std::vector<EvaluatedTerrain> tiles(keys.size());
    ParallelFor(keys.size(), threads,
                [&](std::size_t n) { tiles[n] = EvaluateTile(terrain, scheme, keys[n], 1); });
    return tiles;
}

float EvaluateAt(const Terrain& terrain, double x, double y, double spacing) {
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    if (!terrain.filters.empty()) {
        throw std::invalid_argument(
            "Point queries cannot run filters, which read neighbouring cells; evaluate a grid");
    }
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    float height = terrain.source->HeightAt(x, y);
    for (const auto& layer : terrain.layers) {
        if (!layer) {
            throw std::invalid_argument("A terrain's layers must not be null");
        }
        height = layer->ApplyAt(height, x, y, spacing);
    }
    return height;
}

EvaluatedTerrain Evaluate(const Terrain& terrain) {
    CheckTerrain(terrain);
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
