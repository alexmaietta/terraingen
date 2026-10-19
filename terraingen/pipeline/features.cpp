#include "terraingen/pipeline/features.h"

namespace terraingen {

CraterFeatures PlacedCraters(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return PlacedCraters(terrain.layers, grid, ListedSpacing(grid, spacing, terrain.surface));
}

BoulderFeatures PlacedBoulders(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    const double resolved = ListedSpacing(grid, spacing, terrain.surface);
    BoulderFeatures placed;
    ForEachLayer<BoulderPlacer>(terrain, [&](const BoulderPlacer& placer, std::size_t n) {
        const TerrainGround ground(terrain, n);
        placed.Append(placer.BouldersOver(grid, resolved, &ground));
    });
    return placed;
}

}  // namespace terraingen
