#include "terraingen/pipeline/features.h"

#include <algorithm>

namespace terraingen {

CraterFeatures PlacedCraters(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return PlacedCraters(terrain.layers, grid, std::max(spacing, grid.Spacing(terrain.surface)));
}

BoulderFeatures PlacedBoulders(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    const double resolved = std::max(spacing, grid.Spacing(terrain.surface));
    BoulderFeatures placed;
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        if (const auto* placer = dynamic_cast<const BoulderPlacer*>(terrain.layers[n].get())) {
            const TerrainGround ground(terrain, n);
            placed.Append(placer->BouldersOver(grid, resolved, &ground));
        }
    }
    return placed;
}

}  // namespace terraingen
