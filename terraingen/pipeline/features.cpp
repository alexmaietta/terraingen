#include "terraingen/pipeline/features.h"

namespace terraingen {

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
