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

/// @brief The craters drawn in each cell of each band, kept so tiles and point queries generate
/// each cell once.
///
/// Holds about `maxCraters` craters at most, evicting least recently used cells; thread-safe.
/// Cells regenerate identically, so eviction never changes results.
class CraterCellCache {
  public:
    /// @brief Makes an empty cache.
    /// @param maxCraters Most craters held across every cell; positive.
    /// @throws std::invalid_argument if `maxCraters` is zero.
    explicit CraterCellCache(std::size_t maxCraters = 4000000);

    /// @brief The craters of a cell, generated when not held.
    /// @param exponent The size band's exponent.
    /// @param cell The cell.
    /// @param generate Draws the cell's craters.
    /// @return The cell's craters.
    std::shared_ptr<const CraterList> Get(int exponent, const CellIndex& cell,
                                          const std::function<CraterList()>& generate) {
        return cache_.Get({exponent, cell.i, cell.j}, generate);
    }

    /// @brief Craters held across every cell.
    /// @return Their number.
    std::size_t Size() const {
        return cache_.Size();
    }

    /// @brief The capacity.
    /// @return The most craters held.
    std::size_t MaxCraters() const {
        return cache_.Capacity();
    }

    /// @brief Cells evicted since construction.
    /// @return Their number.
    std::size_t Evictions() const {
        return cache_.Evictions();
    }

  private:
    struct Key {
        int exponent;
        std::int32_t i;
        std::int32_t j;

        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    LruCache<Key, CraterList, KeyHash> cache_;
};

/// @brief A source of cell craters that draws each cell with `GenerateCellCraters` once and keeps
/// it in `cache`; every argument must outlive the source.
/// @param cache The cache the cells are kept in.
/// @param cells The cells craters are drawn in.
/// @param density The crater density.
/// @param ages The age model.
/// @param seed The generation seed.
/// @return The source, for `CraterGenerationOptions::cellCraters`.
CellCraterSource CachedCellCraters(CraterCellCache& cache, const SurfaceCells& cells,
                                   const SizeFrequencyDistribution& density,
                                   const CraterAgeModel& ages, std::uint32_t seed);

}  // namespace terraingen
