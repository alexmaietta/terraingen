#pragma once

#include "terraingen/layers/craters/population.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

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
                                          const std::function<CraterList()>& generate);

    /// Craters held across every cell.
    std::size_t Size() const;

    std::size_t MaxCraters() const {
        return maxCraters_;
    }

    /// Cells evicted since construction.
    std::size_t Evictions() const;

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
    struct Shard {
        mutable std::mutex mutex;
        std::list<Key> recency;
        struct Entry {
            std::shared_ptr<const CraterList> craters;
            std::list<Key>::iterator recent;
        };
        std::unordered_map<Key, Entry, KeyHash> entries;
        std::size_t craters = 0;
        std::size_t evictions = 0;
    };
    static constexpr std::size_t kShards = 16;

    std::size_t maxCraters_;
    std::array<Shard, kShards> shards_;
};

}  // namespace terraingen
