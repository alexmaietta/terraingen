#include "terraingen/pipeline/tile_cache.h"

#include <utility>

namespace terraingen {

namespace {

std::size_t TileBytes(const EvaluatedTerrain& tile) {
    return tile.heights.Size() * sizeof(float) + tile.features.craters.Size() * sizeof(Crater);
}

}  // namespace

TileCache::TileCache(Terrain terrain, TileScheme scheme, std::size_t maxBytes)
    : terrain_(std::move(terrain)), scheme_(scheme), cache_(maxBytes, TileBytes) {
    scheme_.Validate();
    FilterMargin(terrain_, scheme_.Spacing(0));
}

std::shared_ptr<const EvaluatedTerrain> TileCache::Get(const TileKey& key) {
    return cache_.Get(key, [&] { return EvaluateTile(terrain_, scheme_, key, 1); });
}

}  // namespace terraingen
