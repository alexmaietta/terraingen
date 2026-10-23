// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/parallel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <thread>
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

TEST(ParallelFor, NestsAndOverlapsWithoutDeadlock) {
    std::atomic<int> total{0};
    ParallelFor(16, 0,
                [&](std::size_t) { ParallelFor(64, 0, [&](std::size_t) { total.fetch_add(1); }); });
    EXPECT_EQ(total.load(), 16 * 64);
    std::vector<std::thread> callers;
    std::atomic<int> concurrent{0};
    for (int c = 0; c < 4; ++c) {
        callers.emplace_back([&] {
            for (int round = 0; round < 50; ++round) {
                ParallelFor(32, 0, [&](std::size_t) { concurrent.fetch_add(1); });
            }
        });
    }
    for (std::thread& caller : callers) {
        caller.join();
    }
    EXPECT_EQ(concurrent.load(), 4 * 50 * 32);
}

TEST(ParallelFor, KeepsWorkingAfterAFailure) {
    EXPECT_THROW(ParallelFor(1000, 0, [](std::size_t) { throw std::runtime_error("all"); }),
                 std::runtime_error);
    std::atomic<int> runs{0};
    ParallelFor(1000, 0, [&](std::size_t) { runs.fetch_add(1); });
    EXPECT_EQ(runs.load(), 1000);
}
