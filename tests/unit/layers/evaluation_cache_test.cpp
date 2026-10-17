#include "terraingen/layers/evaluation_cache.h"

#include <gtest/gtest.h>

#include <cstdint>

TEST(EvaluationCache, ComputesEachValueOncePerOwnerIdAndSpacing) {
    terraingen::EvaluationCache cache;
    int calls = 0;
    const int a = 0;
    const int b = 0;
    const auto get = [&](const void* owner, std::uint64_t id, double spacing) {
        return *cache.Get<double>(owner, id, spacing, [&] {
            ++calls;
            return static_cast<double>(id) + spacing;
        });
    };
    EXPECT_EQ(get(&a, 7, 1.0), 8.0);
    EXPECT_EQ(get(&a, 7, 1.0), 8.0);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(get(&a, 7, 2.0), 9.0) << "another spacing";
    EXPECT_EQ(get(&b, 7, 1.0), 8.0) << "another owner";
    EXPECT_EQ(get(&a, 8, 1.0), 9.0) << "another feature";
    EXPECT_EQ(calls, 4);
}
