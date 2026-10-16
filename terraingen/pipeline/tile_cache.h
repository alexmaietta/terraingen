/// @file
/// @brief A thread-safe cache of a terrain's tiles, evaluated on demand.
#pragma once

#include "terraingen/pipeline/terrain.h"
#include "terraingen/pipeline/tiling.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <memory>

namespace terraingen {

/// @brief Tiles of one terrain and scheme, evaluated on demand with `EvaluateTile` and kept up to
/// a byte budget, least recently used first out. Safe to use from several threads at once; tiles
/// are re-evaluated identically, so eviction never changes results.
class TileCache {
  public:
    /// @brief A cache of a terrain's tiles.
    /// @param terrain The terrain.
    /// @param scheme The tile scheme.
    /// @param maxBytes The byte budget.
    /// @throws std::invalid_argument for an invalid scheme or terrain, or a zero `maxBytes`.
    TileCache(Terrain terrain, TileScheme scheme, std::size_t maxBytes = std::size_t{1} << 30);

    /// @brief A tile, evaluated on one thread when it is not held.
    /// @param key The tile.
    /// @return The evaluated tile.
    /// @throws std::invalid_argument for a level outside the scheme's; and whatever the terrain's
    /// parts throw.
    std::shared_ptr<const EvaluatedTerrain> Get(const TileKey& key);

    /// @brief Bytes held: four per height plus the features' storage.
    /// @return The bytes held.
    std::size_t Size() const {
        return cache_.Size();
    }

    /// @brief The byte budget.
    /// @return The budget given at construction.
    std::size_t MaxBytes() const {
        return cache_.Capacity();
    }

    /// @brief Tiles evicted since construction.
    /// @return The eviction count.
    std::size_t Evictions() const {
        return cache_.Evictions();
    }

    /// @brief The tile scheme.
    /// @return The scheme given at construction.
    const TileScheme& Scheme() const {
        return scheme_;
    }

  private:
    Terrain terrain_;
    TileScheme scheme_;
    LruCache<TileKey, EvaluatedTerrain, TileKeyHash> cache_;
};

}  // namespace terraingen
