#include "generation_worker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using terraingen::Raster;
using terraingen::viewer::GenerateTerrain;
using terraingen::viewer::GenerationResult;
using terraingen::viewer::GenerationWorker;
using terraingen::viewer::TerrainSettings;

namespace {

std::optional<GenerationResult> WaitForResult(GenerationWorker& worker) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = worker.TakeResult()) {
            return result;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

TerrainSettings Small(std::uint32_t seed) {
    TerrainSettings settings;
    settings.size = 17;
    settings.layers[0].layer.fractal.seed = seed;
    return settings;
}

}  // namespace

TEST(GenerationWorker, ProducesTheSameTerrainAsGeneratingDirectly) {
    GenerationWorker worker;
    const std::uint64_t id = worker.Request(Small(4));
    const auto result = WaitForResult(worker);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, id);
    EXPECT_EQ(result->error, "");
    EXPECT_EQ(result->settings, Small(4));
    EXPECT_EQ(result->stats.validCount, 17u * 17u);
    EXPECT_GE(result->milliseconds, 0.0);

    const auto expected = GenerateTerrain(Small(4));
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(result->raster.Values()[i], expected.Values()[i]);
    }
    EXPECT_FALSE(worker.TakeResult().has_value());
}

TEST(GenerationWorker, ReportsGenerationErrors) {
    GenerationWorker worker([](const TerrainSettings&) -> Raster<float> {
        throw std::invalid_argument("bad settings");
    });
    worker.Request(Small(1));
    const auto result = WaitForResult(worker);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->error, "bad settings");
}

TEST(GenerationWorker, LatestRequestWins) {
    std::atomic<bool> release{false};
    std::mutex callsMutex;
    std::vector<std::uint32_t> calls;
    GenerationWorker worker([&](const TerrainSettings& settings) {
        {
            const std::lock_guard lock(callsMutex);
            calls.push_back(settings.layers[0].layer.fractal.seed);
        }
        while (!release.load()) {
            std::this_thread::yield();
        }
        return Raster<float>(2, 2, static_cast<float>(settings.layers[0].layer.fractal.seed));
    });

    worker.Request(Small(1));
    while (true) {
        const std::lock_guard lock(callsMutex);
        if (!calls.empty()) {
            break;
        }
    }
    worker.Request(Small(2));
    const std::uint64_t last = worker.Request(Small(3));
    EXPECT_TRUE(worker.Busy());
    release = true;

    const auto result = WaitForResult(worker);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, last);
    EXPECT_EQ(result->raster(0, 0), 3.0f);

    while (worker.Busy()) {
        std::this_thread::yield();
    }
    const std::lock_guard lock(callsMutex);
    EXPECT_EQ(calls, (std::vector<std::uint32_t>{1u, 3u}));
}

TEST(GenerationWorker, StopsCleanlyWithWorkOutstanding) {
    {
        GenerationWorker worker([](const TerrainSettings&) { return Raster<float>(2, 2); });
        worker.Request(Small(1));
        worker.Request(Small(2));
    }
    SUCCEED();
}
