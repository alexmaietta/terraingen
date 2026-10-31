// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/lru_cache.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using terraingen::LruCache;

TEST(LruCache, ComputesOnceAndEvictsWithinTheBudget) {
    std::size_t evicted = 0;
    LruCache<int, std::string> cache(
        64, [](const std::string& value) { return value.size(); },
        [&](std::size_t count) { evicted += count; });
    int calls = 0;
    const auto first = cache.Get(1, [&] {
        ++calls;
        return std::string("abcd");
    });
    EXPECT_EQ(cache.Get(1,
                        [&] {
                            ++calls;
                            return std::string("other");
                        }),
              first);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(cache.Size(), 4u);
    for (int key = 2; key < 200; ++key) {
        cache.Get(key, [] { return std::string(4, 'x'); });
    }
    EXPECT_LE(cache.Size(), 64u);
    EXPECT_GT(evicted, 0u);
    EXPECT_EQ(cache.Size() + 4 * evicted, 4u * 199);
    EXPECT_THROW((LruCache<int, int>(0, [](const int&) { return std::size_t{1}; })),
                 std::invalid_argument);
}

TEST(LruCache, OneShardEvictsInStrictOrderOfUse) {
    LruCache<int, std::string> cache(
        8, [](const std::string& value) { return value.size(); }, {}, 1);
    int calls = 0;
    const auto get = [&](int key) {
        return cache.Get(key, [&] {
            ++calls;
            return std::string(4, 'x');
        });
    };
    get(1);
    get(2);
    get(1);
    get(3);
    EXPECT_EQ(calls, 3);
    EXPECT_NE(cache.Find(1), nullptr) << "used since 2";
    EXPECT_EQ(cache.Find(2), nullptr) << "the least recently used goes first";
    EXPECT_NE(cache.Find(3), nullptr);
    EXPECT_THROW((LruCache<int, int>(
                     4, [](const int&) { return std::size_t{1}; }, {}, 0)),
                 std::invalid_argument);
}
