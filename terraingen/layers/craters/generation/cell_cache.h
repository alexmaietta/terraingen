// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `CraterCellCache`, the craters of generated cells kept for reuse.
#pragma once

#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace terraingen {

/// The craters drawn in each cell of each band, kept so tiles and point queries generate
/// each cell once.
///
/// Holds about `maxCraters` craters at most, evicting least recently used cells; thread-safe.
/// Cells regenerate identically, so eviction never changes results.
class CraterCellCache {
  public:
    /// @param maxCraters Most craters held across every cell; positive.
    /// @throws std::invalid_argument if `maxCraters` is zero.
    explicit CraterCellCache(std::size_t maxCraters = 4000000);

    /// The craters of `cell` in the band of `exponent`, drawn by `generate` when not held.
    std::shared_ptr<const CellCraters> Get(int exponent, const CellIndex& cell,
                                           const std::function<CellCraters()>& generate) {
        return cache_.Get({exponent, cell.i, cell.j}, generate);
    }

  private:
    struct Key {
        int exponent;
        std::int32_t i;
        std::int32_t j;

        bool operator==(const Key& other) const {
            return exponent == other.exponent && i == other.i && j == other.j;
        }
        bool operator!=(const Key& other) const {
            return !(*this == other);
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    LruCache<Key, CellCraters, KeyHash> cache_;
};

/// A source for `CraterGenerationOptions::cellCraters` that draws each cell with
/// `GenerateCellCraters` once and keeps it in `cache`; every argument must outlive it.
CellCraterSource CachedCellCraters(CraterCellCache& cache, const SurfaceCells& cells,
                                   const SizeFrequencyDistribution& density,
                                   const CraterAgeModel& ages, std::uint32_t seed);

/// `GenerateCraters` drawing each cell through `cache` (`CachedCellCraters`), or as `options`
/// say when `cache` is null.
/// @throws std::invalid_argument as `GenerateCraters`.
/// @throws std::domain_error as `GenerateCraters`.
CraterList GenerateCachedCraters(CraterCellCache* cache, const SurfaceCells& cells,
                                 const Bounds2d& region, const SizeFrequencyDistribution& density,
                                 const CraterAgeModel& ages, std::uint32_t seed,
                                 CraterGenerationOptions options);

}  // namespace terraingen
