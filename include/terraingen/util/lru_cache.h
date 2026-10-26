// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `LruCache`, a sharded thread-safe least-recently-used cache.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace terraingen {

/// A thread-safe cache of recomputable values, holding about `capacity` units of cost and
/// evicting the least recently used.
///
/// Keys are spread over 16 shards with even shares of the capacity; a shard always keeps its
/// newest value, so it can exceed its share by one value. If two threads compute the same key at
/// once, both get the first value stored.
template <typename Key, typename Value, typename Hash = std::hash<Key>> class LruCache {
  public:
    /// Measures a value's cost (a count or bytes).
    using Cost = std::function<std::size_t(const Value&)>;
    /// Told how many values one store evicted.
    using OnEvict = std::function<void(std::size_t evicted)>;

    /// An empty cache.
    /// @param capacity The total cost held, in `cost` units.
    /// @param onEvict If given, told how many values a store evicted, outside any lock.
    /// @throws std::invalid_argument if `capacity` is zero.
    LruCache(std::size_t capacity, Cost cost, OnEvict onEvict = {})
        : capacity_(capacity), cost_(std::move(cost)), onEvict_(std::move(onEvict)) {
        if (capacity_ == 0) {
            throw std::invalid_argument("A cache needs a positive capacity");
        }
    }

    /// The value held for a key, marked most recently used; null when it is not held.
    std::shared_ptr<const Value> Find(const Key& key) {
        return Lookup(ShardHolding(key), key);
    }

    /// The value for a key, computed by `compute` outside the lock when it is not held.
    /// @throws Whatever `compute` throws; nothing is stored then.
    std::shared_ptr<const Value> Get(const Key& key, const std::function<Value()>& compute) {
        Shard& shard = ShardHolding(key);
        if (auto held = Lookup(shard, key)) {
            return held;
        }
        auto value = std::make_shared<const Value>(compute());
        const std::size_t cost = cost_(*value);
        const std::size_t budget = std::max<std::size_t>(1, capacity_ / kShards);
        std::size_t evicted = 0;
        {
            const std::lock_guard lock(shard.mutex);
            if (const auto found = shard.entries.find(key); found != shard.entries.end()) {
                return found->second.value;
            }
            shard.recency.push_front(key);
            shard.entries.emplace(key, Entry{value, cost, shard.recency.begin()});
            shard.cost += cost;
            while (shard.cost > budget && shard.recency.size() > 1) {
                const auto oldest = shard.entries.find(shard.recency.back());
                shard.cost -= oldest->second.cost;
                shard.entries.erase(oldest);
                shard.recency.pop_back();
                ++evicted;
            }
        }
        if (evicted > 0 && onEvict_) {
            onEvict_(evicted);
        }
        return value;
    }

    /// The total cost of every value held, in `Cost` units.
    std::size_t Size() const {
        std::size_t total = 0;
        for (const Shard& shard : shards_) {
            const std::lock_guard lock(shard.mutex);
            total += shard.cost;
        }
        return total;
    }

  private:
    struct Entry {
        std::shared_ptr<const Value> value;
        std::size_t cost = 0;
        typename std::list<Key>::iterator recent;
    };
    struct Shard {
        mutable std::mutex mutex;
        std::list<Key> recency;
        std::unordered_map<Key, Entry, Hash> entries;
        std::size_t cost = 0;
    };
    static constexpr std::size_t kShards = 16;

    Shard& ShardHolding(const Key& key) {
        return shards_[Hash{}(key) % kShards];
    }

    /// The value `shard` holds for a key, marked most recently used; null when it is not held.
    static std::shared_ptr<const Value> Lookup(Shard& shard, const Key& key) {
        const std::lock_guard lock(shard.mutex);
        const auto found = shard.entries.find(key);
        if (found == shard.entries.end()) {
            return nullptr;
        }
        shard.recency.splice(shard.recency.begin(), shard.recency, found->second.recent);
        return found->second.value;
    }

    std::size_t capacity_;
    Cost cost_;
    OnEvict onEvict_;
    std::array<Shard, kShards> shards_;
};

}  // namespace terraingen
