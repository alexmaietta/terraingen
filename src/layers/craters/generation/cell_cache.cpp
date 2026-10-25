// SPDX-License-Identifier: BSD-3-Clause

#include "layers/craters/generation/cell_cache.h"

#include "terraingen/noise/hash.h"
#include "terraingen/util/log.h"

#include <array>
#include <atomic>

namespace terraingen {

namespace {

/// Cells each thread keeps of the caches it last read.
constexpr std::size_t kRecentCells = 256;

/// The source of each cache's `id_`, from 1 so 0 marks an empty slot.
std::atomic<std::uint64_t> nextCacheId{1};

/// A cell a thread read: its cache, band and index, and its craters.
struct RecentCell {
    std::uint64_t cache = 0;
    int exponent = 0;
    std::int32_t i = 0;
    std::int32_t j = 0;
    std::shared_ptr<const CellCraters> craters;
};

}  // namespace

std::shared_ptr<const CellCraters>
CraterCellCache::Get(int exponent, const CellIndex& cell,
                     const std::function<CellCraters()>& generate) {
    thread_local std::array<RecentCell, kRecentCells> recent;
    const std::uint32_t mixed = (static_cast<std::uint32_t>(cell.i) * 0x9E3779B1u) ^
                                (static_cast<std::uint32_t>(cell.j) * 0x85EBCA77u) ^
                                (static_cast<std::uint32_t>(exponent) * 0xC2B2AE3Du);
    RecentCell& slot = recent[(mixed >> 16U) % kRecentCells];
    if (slot.cache == id_ && slot.exponent == exponent && slot.i == cell.i && slot.j == cell.j) {
        return slot.craters;
    }
    std::shared_ptr<const CellCraters> craters = cache_.Get({exponent, cell.i, cell.j}, generate);
    slot = {id_, exponent, cell.i, cell.j, craters};
    return craters;
}

std::size_t CraterCellCache::KeyHash::operator()(const Key& key) const {
    return HashWords({static_cast<std::uint64_t>(key.exponent), static_cast<std::uint64_t>(key.i),
                      static_cast<std::uint64_t>(key.j)});
}

CraterCellCache::CraterCellCache(std::size_t maxCraters)
    : cache_(
          maxCraters, [](const CellCraters& cell) { return cell.craters.Size(); },
          [maxCraters](std::size_t evicted) {
              LogDebug("craters", "Crater cache evicted ", evicted, " cells to stay within ",
                       maxCraters, " craters");
          }),
      id_(nextCacheId.fetch_add(1)) {}

CellCraterSource CachedCellCraters(CraterCellCache& cache, const SurfaceCells& cells,
                                   const SizeFrequencyDistribution& density,
                                   const CraterAgeModel& ages, std::uint32_t seed) {
    return [&cache, &cells, &density, &ages, seed](const CraterBand& band, const CellIndex& cell) {
        return cache.Get(band.exponent, cell, [&] {
            return CellCraters(GenerateCellCraters(cells, band, cell, density, ages, seed));
        });
    };
}

CraterList GenerateCachedCraters(CraterCellCache* cache, const SurfaceCells& cells,
                                 const Bounds2d& region, const SizeFrequencyDistribution& density,
                                 const CraterAgeModel& ages, std::uint32_t seed,
                                 CraterGenerationOptions options) {
    if (cache != nullptr) {
        options.cellCraters = CachedCellCraters(*cache, cells, density, ages, seed);
    }
    return GenerateCraters(cells, region, density, ages, seed, options);
}

}  // namespace terraingen
