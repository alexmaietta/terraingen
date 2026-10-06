#include "terraingen/layers/craters/cell_cache.h"

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <stdexcept>

namespace terraingen {

std::size_t CraterCellCache::KeyHash::operator()(const Key& key) const {
    const UVec3 hash =
        Pcg3dHash({static_cast<std::uint32_t>(key.exponent), static_cast<std::uint32_t>(key.i),
                   static_cast<std::uint32_t>(key.j)});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

CraterCellCache::CraterCellCache(std::size_t maxCraters) : maxCraters_(maxCraters) {
    if (maxCraters_ == 0) {
        throw std::invalid_argument("A crater cache must hold at least one crater");
    }
}

std::shared_ptr<const CraterList>
CraterCellCache::Get(int exponent, const CellIndex& cell,
                     const std::function<CraterList()>& generate) {
    const Key key{exponent, cell.i, cell.j};
    Shard& shard = shards_[KeyHash{}(key) % kShards];
    {
        const std::lock_guard lock(shard.mutex);
        if (const auto found = shard.entries.find(key); found != shard.entries.end()) {
            shard.recency.splice(shard.recency.begin(), shard.recency, found->second.recent);
            return found->second.craters;
        }
    }
    auto craters = std::make_shared<const CraterList>(generate());
    const std::size_t budget = std::max<std::size_t>(1, maxCraters_ / kShards);
    const std::lock_guard lock(shard.mutex);
    if (const auto found = shard.entries.find(key); found != shard.entries.end()) {
        return found->second.craters;
    }
    shard.recency.push_front(key);
    shard.entries.emplace(key, Shard::Entry{craters, shard.recency.begin()});
    shard.craters += craters->Size();
    std::size_t evicted = 0;
    while (shard.craters > budget && shard.recency.size() > 1) {
        const Key oldest = shard.recency.back();
        const auto entry = shard.entries.find(oldest);
        shard.craters -= entry->second.craters->Size();
        shard.entries.erase(entry);
        shard.recency.pop_back();
        ++evicted;
    }
    if (evicted > 0) {
        shard.evictions += evicted;
        LogDebug("craters", "Crater cache evicted ", evicted, " cells to stay within ", maxCraters_,
                 " craters");
    }
    return craters;
}

std::size_t CraterCellCache::Size() const {
    std::size_t total = 0;
    for (const Shard& shard : shards_) {
        const std::lock_guard lock(shard.mutex);
        total += shard.craters;
    }
    return total;
}

std::size_t CraterCellCache::Evictions() const {
    std::size_t total = 0;
    for (const Shard& shard : shards_) {
        const std::lock_guard lock(shard.mutex);
        total += shard.evictions;
    }
    return total;
}

}  // namespace terraingen
