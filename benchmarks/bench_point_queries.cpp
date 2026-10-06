#include "fixtures.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <vector>

namespace terraingen::benchmarks {

namespace {

/// Points on a jittered 0.25 m lattice over a 20 m patch, as SCM's nodes under a rover.
std::vector<Vec2d> Patch(double centreX, double centreY) {
    std::vector<Vec2d> points;
    for (int j = 0; j < 80; ++j) {
        for (int i = 0; i < 80; ++i) {
            points.push_back({centreX - 10.0 + 0.25 * i + 0.01 * (j % 7),
                              centreY - 10.0 + 0.25 * j + 0.01 * (i % 5)});
        }
    }
    return points;
}

/// SCM-style point queries at 0.25 m spacing with the cell cache warm; with threads, every thread
/// queries the same terrain.
void BM_EvaluateAtWarm(benchmark::State& state) {
    static const Terrain terrain = LunarMare(4096.0, 1.0);
    const std::vector<Vec2d> points = Patch(100.0 * state.thread_index(), -50.0);
    for (const Vec2d& point : points) {
        EvaluateAt(terrain, point.x, point.y, 0.25);
    }
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(EvaluateAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_EvaluateAtWarm)->ThreadRange(1, 8)->UseRealTime();

/// Queries with no cell cache, so every point regenerates the cells around it, over the first 64
/// points of a patch.
void BM_EvaluateAtUncached(benchmark::State& state) {
    const Terrain terrain = LunarMare(4096.0, 1.0, 200.0, 0);
    std::vector<Vec2d> points = Patch(0.0, 0.0);
    points.resize(64);
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(EvaluateAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_EvaluateAtUncached);

/// A patch never queried before: cells are generated on the way, as when a rover drives onto new
/// ground.
void BM_EvaluateAtCold(benchmark::State& state) {
    const Terrain terrain = LunarMare(4096.0, 1.0);
    double x = 0.0;
    std::size_t queries = 0;
    for (auto _ : state) {
        for (const Vec2d& point : Patch(x, 300.0)) {
            benchmark::DoNotOptimize(EvaluateAt(terrain, point.x, point.y, 0.25));
            ++queries;
        }
        x += 400.0;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(queries));
}
BENCHMARK(BM_EvaluateAtCold);

}  // namespace

}  // namespace terraingen::benchmarks
