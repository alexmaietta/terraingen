// SPDX-License-Identifier: BSD-3-Clause

#include "fixtures.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace terraingen::benchmarks {

namespace {

/// A 100 m lunar mare at 2 m cells, as the Martian scene's lower layers, under `readers` layers
/// that read the ground beneath them at many points per sample: a pavement on exposed bedrock
/// (nine points 15 m around), then sand pooled in the lows (sixteen points up to 20 m around).
TerrainGenerator GroundReaders(int readers) {
    TerrainGenerator terrain = LunarMare(100.0, 2.0, 120.0);
    if (readers >= 1) {
        PavementOptions pavement;
        pavement.plateSize = 3.0;
        pavement.crackWidth = 0.15;
        pavement.crackDepth = 0.12;
        pavement.seed = 4;
        ExposureParams exposure;
        exposure.reach = 15.0;
        pavement.exposure = exposure;
        terrain.layers.push_back(std::make_shared<PavementLayer>(pavement));
    }
    if (readers >= 2) {
        SandFillOptions sand;
        sand.reach = 20.0;
        sand.fill = 0.4;
        sand.maxThickness = 0.8;
        terrain.layers.push_back(std::make_shared<SandFillLayer>(sand));
    }
    return terrain;
}

/// The grid of `GroundReaders` (the first argument) on every hardware thread: each layer that reads
/// the ground reads it in a few batches per block of samples, not point by point, so its cost adds
/// to the stack's rather than multiplying it.
void BM_EvaluateGroundReaders(benchmark::State& state) {
    const TerrainGenerator terrain = GroundReaders(static_cast<int>(state.range(0)));
    const RasterGrid grid = terrain.source->Grid();
    for (auto _ : state) {
        benchmark::DoNotOptimize(Evaluate(terrain, grid));
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(grid.width * grid.height));
}
BENCHMARK(BM_EvaluateGroundReaders)
    ->DenseRange(0, 2)
    ->ArgNames({"readers"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

/// Point queries of `GroundReaders` with both readers, 1000 points scattered over the mare at 2 m.
void BM_HeightsAtGroundReaders(benchmark::State& state) {
    const TerrainGenerator terrain = GroundReaders(2);
    std::vector<Vec2d> points;
    for (int i = 0; i < 1000; ++i) {
        points.push_back({0.097 * i - 48.0, 0.89 * (i % 107) - 47.0});
    }
    for (auto _ : state) {
        EvaluationCache cache;
        benchmark::DoNotOptimize(HeightsAt(terrain, points, 2.0, 0, &cache));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}
BENCHMARK(BM_HeightsAtGroundReaders)->Unit(benchmark::kMillisecond)->UseRealTime();

}  // namespace

}  // namespace terraingen::benchmarks
