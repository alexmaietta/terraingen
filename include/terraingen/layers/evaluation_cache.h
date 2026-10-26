// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `EvaluationCache`, values one evaluation's batches of points share.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/lru_cache.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace terraingen {

/// Thread-safe values that depend only on the terrain beneath a layer, such as crater datums,
/// shared by point queries; use one cache per generator and renew it after changing its layers.
class EvaluationCache {
  public:
    /// An empty cache holding at most `capacity` values.
    explicit EvaluationCache(std::size_t capacity = 1 << 20)
        : values_(capacity, [](const Holder&) { return std::size_t{1}; }), id_(NextId()) {}

    /// This cache among every one made, never 0 and never reused, so values a thread keeps
    /// for it can be told from another's.
    std::uint64_t Id() const {
        return id_;
    }

    /// The value of `owner` for feature `id` at `spacing`, computed by `compute` when not held.
    template <typename T>
    std::shared_ptr<const T> Get(const void* owner, std::uint64_t id, double spacing,
                                 const std::function<T()>& compute) {
        const Key key{reinterpret_cast<std::uintptr_t>(owner), id, BitCast<std::uint64_t>(spacing)};
        const std::shared_ptr<const Holder> held =
            values_.Get(key, [&] { return Holder{std::make_shared<const T>(compute())}; });
        return std::static_pointer_cast<const T>(held->value);
    }

    /// The value of `owner` for feature `id` at `spacing` when held, else null.
    template <typename T>
    std::shared_ptr<const T> Find(const void* owner, std::uint64_t id, double spacing) {
        const Key key{reinterpret_cast<std::uintptr_t>(owner), id, BitCast<std::uint64_t>(spacing)};
        const std::shared_ptr<const Holder> held = values_.Find(key);
        return held ? std::static_pointer_cast<const T>(held->value) : nullptr;
    }

  private:
    struct Key {
        std::uintptr_t owner = 0;
        std::uint64_t id = 0;
        std::uint64_t spacing = 0;

        bool operator==(const Key& other) const {
            return owner == other.owner && id == other.id && spacing == other.spacing;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const {
            return HashWords({static_cast<std::uint64_t>(key.owner), key.id, key.spacing});
        }
    };
    struct Holder {
        std::shared_ptr<const void> value;
    };

    static std::uint64_t NextId() {
        static std::atomic<std::uint64_t> next{1};
        return next.fetch_add(1);
    }

    LruCache<Key, Holder, KeyHash> values_;
    std::uint64_t id_;
};

}  // namespace terraingen
