#include "terraingen/pipeline/terrain.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    if (std::any_of(terrain.layers.begin(), terrain.layers.end(),
                    [](const auto& l) { return !l; }) ||
        std::any_of(terrain.filters.begin(), terrain.filters.end(),
                    [](const auto& f) { return !f; })) {
        throw std::invalid_argument("A terrain's layers and filters must not be null");
    }
    if (!(std::isfinite(spacing) && spacing >= 0.0)) {
        throw std::invalid_argument("Sample spacing must be finite and not negative");
    }
    const double resolved = std::max(spacing, grid.PixelSize());
    EvaluatedTerrain result{grid.MakeRaster(), {}};
    terrain.source->Fill(result.heights);
    for (const auto& layer : terrain.layers) {
        layer->Apply(result.heights, resolved, result.features);
    }
    for (const auto& filter : terrain.filters) {
        filter->Apply(result.heights, resolved, result.features);
    }
    return result;
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
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
