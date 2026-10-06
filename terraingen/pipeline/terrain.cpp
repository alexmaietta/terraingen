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

EvaluatedTerrain Evaluate(const Terrain& terrain) {
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    return Evaluate(terrain, terrain.source->Grid());
}

}  // namespace terraingen
