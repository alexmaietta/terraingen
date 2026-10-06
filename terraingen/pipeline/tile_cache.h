#pragma once

#include "terraingen/pipeline/terrain.h"
#include "terraingen/pipeline/tiling.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <memory>

namespace terraingen {

/// Tiles of one terrain and scheme, evaluated on demand with `EvaluateTile` and kept up to a byte
/// budget, least recently used first out. Safe to use from several threads at once; since tiles are
/// re-evaluated identically, eviction never changes results.
class TileCache {
  public:
    /// @throws std::invalid_argument for an invalid scheme or terrain, or a zero `maxBytes`.
    TileCache(Terrain terrain, TileScheme scheme, std::size_t maxBytes = std::size_t{1} << 30);

    /// The tile at `key`, evaluated on one thread when it is not held.
    std::shared_ptr<const EvaluatedTerrain> Get(const TileKey& key);

    /// Bytes held: four per height and the craters' storage.
    std::size_t Size() const {
        return cache_.Size();
    }

    std::size_t MaxBytes() const {
        return cache_.Capacity();
    }

    /// Tiles evicted since construction.
    std::size_t Evictions() const {
        return cache_.Evictions();
    }

    const TileScheme& Scheme() const {
        return scheme_;
    }

  private:
    Terrain terrain_;
    TileScheme scheme_;
    LruCache<TileKey, EvaluatedTerrain, TileKeyHash> cache_;
};

}  // namespace terraingen
