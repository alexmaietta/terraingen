#include "terraingen/util/parallel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <vector>

using terraingen::ParallelFor;

TEST(ParallelFor, RunsEveryTaskOnce) {
    for (const unsigned threads : {0u, 1u, 3u, 16u}) {
        std::vector<std::atomic<int>> runs(1000);
        ParallelFor(runs.size(), threads, [&](std::size_t i) { runs[i].fetch_add(1); });
        for (const auto& count : runs) {
            ASSERT_EQ(count.load(), 1) << threads << " threads";
        }
    }
    int calls = 0;
    ParallelFor(0, 4, [&](std::size_t) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(ParallelFor, RethrowsTheFirstFailure) {
    for (const unsigned threads : {1u, 4u}) {
        EXPECT_THROW(ParallelFor(100, threads,
                                 [](std::size_t i) {
                                     if (i == 37) {
                                         throw std::runtime_error("task 37");
                                     }
                                 }),
                     std::runtime_error);
    }
}

TEST(ResolveThreads, UsesTheRequestOrEveryHardwareThread) {
    EXPECT_EQ(terraingen::ResolveThreads(5), 5u);
    EXPECT_GE(terraingen::ResolveThreads(0), 1u);
}
