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

/// A thread-safe cache of values that can always be recomputed, holding at most about `capacity`
/// units of cost (counts or bytes, as `Cost` measures them) and evicting the least recently used.
///
/// Keys are spread over 16 shards, each with its own lock and an even share of the capacity, so
/// threads working on different keys rarely wait for one another. A shard always keeps its newest
/// value, so values larger than a sixteenth of the capacity can exceed it by up to one value per
/// shard. A value is computed outside the lock; if
/// two threads compute the same key at once, both get the first value stored.
template <typename Key, typename Value, typename Hash = std::hash<Key>> class LruCache {
  public:
    using Cost = std::function<std::size_t(const Value&)>;
    using OnEvict = std::function<void(std::size_t evicted)>;

    /// `onEvict`, if given, is told how many values a store evicted, outside any lock.
    /// @throws std::invalid_argument if `capacity` is zero.
    LruCache(std::size_t capacity, Cost cost, OnEvict onEvict = {})
        : capacity_(capacity), cost_(std::move(cost)), onEvict_(std::move(onEvict)) {
        if (capacity_ == 0) {
            throw std::invalid_argument("A cache needs a positive capacity");
        }
    }

    /// The value for `key`, from `compute` when it is not held.
    std::shared_ptr<const Value> Get(const Key& key, const std::function<Value()>& compute) {
        Shard& shard = shards_[Hash{}(key) % kShards];
        {
            const std::lock_guard lock(shard.mutex);
            if (const auto found = shard.entries.find(key); found != shard.entries.end()) {
                shard.recency.splice(shard.recency.begin(), shard.recency, found->second.recent);
                return found->second.value;
            }
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
            shard.evictions += evicted;
        }
        if (evicted > 0 && onEvict_) {
            onEvict_(evicted);
        }
        return value;
    }

    /// The cost of every value held.
    std::size_t Size() const {
        return Sum([](const Shard& shard) { return shard.cost; });
    }

    /// Values evicted since construction.
    std::size_t Evictions() const {
        return Sum([](const Shard& shard) { return shard.evictions; });
    }

    std::size_t Capacity() const {
        return capacity_;
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
        std::size_t evictions = 0;
    };
    static constexpr std::size_t kShards = 16;

    template <typename Field> std::size_t Sum(Field field) const {
        std::size_t total = 0;
        for (const Shard& shard : shards_) {
            const std::lock_guard lock(shard.mutex);
            total += field(shard);
        }
        return total;
    }

    std::size_t capacity_;
    Cost cost_;
    OnEvict onEvict_;
    std::array<Shard, kShards> shards_;
};

}  // namespace terraingen
