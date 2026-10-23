// SPDX-License-Identifier: BSD-3-Clause

#include "fixtures.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <vector>

namespace terraingen::benchmarks {

namespace {

/// Points on a jittered 0.25 m lattice over a 20 m patch, as SCM's nodes under a rover.
std::vector<Vec2d> Patch(double centerX, double centerY) {
    std::vector<Vec2d> points;
    for (int j = 0; j < 80; ++j) {
        for (int i = 0; i < 80; ++i) {
            points.push_back({centerX - 10.0 + 0.25 * i + 0.01 * (j % 7),
                              centerY - 10.0 + 0.25 * j + 0.01 * (i % 5)});
        }
    }
    return points;
}

/// SCM-style point queries at 0.25 m spacing with the cell cache warm; with threads, every thread
/// queries the same terrain.
void BM_HeightAtWarm(benchmark::State& state) {
    static const TerrainGenerator terrain = LunarMare(4096.0, 1.0);
    const std::vector<Vec2d> points = Patch(100.0 * state.thread_index(), -50.0);
    for (const Vec2d& point : points) {
        HeightAt(terrain, point.x, point.y, 0.25);
    }
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightAtWarm)->ThreadRange(1, 8)->UseRealTime();

/// `BM_HeightAtWarm` on a lunar-sized sphere: the same patch in meters, placed at the equator.
void BM_HeightAtWarmSphere(benchmark::State& state) {
    static const Surface moon = Surface::Sphere(1737400.0);
    static const TerrainGenerator terrain =
        LunarMare(4096.0, 1.0, 200.0, 4000000, CraterDatum::None, moon);
    std::vector<Vec2d> points;
    for (const Vec2d& offset : Patch(100.0 * state.thread_index(), -50.0)) {
        points.push_back(moon.Locate({0.0, 0.0}, offset));
    }
    for (const Vec2d& point : points) {
        HeightAt(terrain, point.x, point.y, 0.25);
    }
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightAtWarmSphere)->ThreadRange(1, 8)->UseRealTime();

/// Warm point queries of craters cut to a plane datum: each crater reaching a point fits its datum
/// to the ground and the craters before it on a 7 x 7 lattice.
void BM_HeightAtDatum(benchmark::State& state) {
    static const TerrainGenerator terrain =
        LunarMare(4096.0, 1.0, 200.0, 4000000, CraterDatum::Plane);
    std::vector<Vec2d> points = Patch(0.0, 0.0);
    points.resize(400);
    for (const Vec2d& point : points) {
        HeightAt(terrain, point.x, point.y, 0.25);
    }
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightAtDatum);

/// `BM_HeightAtDatum` with one `EvaluationCache` kept across calls, as a simulation stepping over
/// the same ground would: datums are fitted once and reused.
void BM_HeightAtDatumCached(benchmark::State& state) {
    static const TerrainGenerator terrain =
        LunarMare(4096.0, 1.0, 200.0, 4000000, CraterDatum::Plane);
    std::vector<Vec2d> points = Patch(0.0, 0.0);
    points.resize(400);
    EvaluationCache cache;
    for (const Vec2d& point : points) {
        HeightAt(terrain, point.x, point.y, 0.25, &cache);
    }
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25, &cache));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightAtDatumCached);

/// Queries with no cell cache, so every point regenerates the cells around it, over the first 64
/// points of a patch.
void BM_HeightAtUncached(benchmark::State& state) {
    const TerrainGenerator terrain = LunarMare(4096.0, 1.0, 200.0, 0);
    std::vector<Vec2d> points = Patch(0.0, 0.0);
    points.resize(64);
    for (auto _ : state) {
        for (const Vec2d& point : points) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightAtUncached);

/// A patch never queried before: cells are generated on the way, as when a rover drives onto new
/// ground.
void BM_HeightAtCold(benchmark::State& state) {
    const TerrainGenerator terrain = LunarMare(4096.0, 1.0);
    double x = 0.0;
    std::size_t queries = 0;
    for (auto _ : state) {
        for (const Vec2d& point : Patch(x, 300.0)) {
            benchmark::DoNotOptimize(HeightAt(terrain, point.x, point.y, 0.25));
            ++queries;
        }
        x += 400.0;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(queries));
}
BENCHMARK(BM_HeightAtCold);

}  // namespace

}  // namespace terraingen::benchmarks
