#pragma once

#include "terraingen/layers/craters/population.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace terraingen {

/// The craters drawn in each cell of each band, kept so that tiles and point queries over the same
/// ground generate each cell once.
///
/// Holds at most about `maxCraters` craters, evicting the least recently used cells, and is safe to
/// use from several threads at once. Since cells are regenerated identically, eviction never
/// changes results.
class CraterCellCache {
  public:
    /// @throws std::invalid_argument if `maxCraters` is zero.
    explicit CraterCellCache(std::size_t maxCraters = 4000000);

    /// The craters of `cell` in band `exponent`, from `generate` when they are not held.
    std::shared_ptr<const CraterList> Get(int exponent, const CellIndex& cell,
                                          const std::function<CraterList()>& generate) {
        return cache_.Get({exponent, cell.i, cell.j}, generate);
    }

    /// Craters held across every cell.
    std::size_t Size() const {
        return cache_.Size();
    }

    std::size_t MaxCraters() const {
        return cache_.Capacity();
    }

    /// Cells evicted since construction.
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

}  // namespace terraingen
