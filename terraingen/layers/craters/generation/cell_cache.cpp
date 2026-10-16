#include "terraingen/layers/craters/generation/cell_cache.h"

#include "terraingen/noise/hash.h"
#include "terraingen/util/log.h"

namespace terraingen {

std::size_t CraterCellCache::KeyHash::operator()(const Key& key) const {
    const UVec3 hash =
        Pcg3dHash({static_cast<std::uint32_t>(key.exponent), static_cast<std::uint32_t>(key.i),
                   static_cast<std::uint32_t>(key.j)});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

CraterCellCache::CraterCellCache(std::size_t maxCraters)
    : cache_(
          maxCraters, [](const CraterList& craters) { return craters.Size(); },
          [maxCraters](std::size_t evicted) {
              LogDebug("craters", "Crater cache evicted ", evicted, " cells to stay within ",
                       maxCraters, " craters");
          }) {}

CellCraterSource CachedCellCraters(CraterCellCache& cache, const SurfaceCells& cells,
                                   const SizeFrequencyDistribution& density,
                                   const CraterAgeModel& ages, std::uint32_t seed) {
    return [&cache, &cells, &density, &ages, seed](const CraterBand& band, const CellIndex& cell) {
        return cache.Get(band.exponent, cell, [&] {
            return GenerateCellCraters(cells, band, cell, density, ages, seed);
        });
    };
}

}  // namespace terraingen
