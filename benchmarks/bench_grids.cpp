#include "fixtures.h"

#include <benchmark/benchmark.h>

namespace terraingen::benchmarks {

namespace {

/// A whole grid (side cells given by the first argument, 1 m cells) on the number of threads given
/// by the second (0 for every hardware thread).
void BM_EvaluateGrid(benchmark::State& state) {
    const auto side = static_cast<double>(state.range(0));
    const Terrain terrain = LunarMare(side, 1.0);
    const RasterGrid grid = terrain.source->Grid();
    const EvaluationOptions options{0.0, static_cast<unsigned>(state.range(1))};
    for (auto _ : state) {
        benchmark::DoNotOptimize(Evaluate(terrain, grid, options));
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(grid.width * grid.height));
}
BENCHMARK(BM_EvaluateGrid)
    ->ArgsProduct({{512, 2048}, {1, 0}})
    ->ArgNames({"cells", "threads"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

/// `BM_EvaluateGrid` on a lunar-sized sphere at the equator, 1 m cells along a meridian.
void BM_EvaluateGridSphere(benchmark::State& state) {
    const auto side = static_cast<double>(state.range(0));
    const Terrain terrain =
        LunarMare(side, 1.0, 200.0, 4000000, CraterDatum::None, Surface::Sphere(1737400.0));
    const RasterGrid grid = terrain.source->Grid();
    const EvaluationOptions options{0.0, static_cast<unsigned>(state.range(1))};
    for (auto _ : state) {
        benchmark::DoNotOptimize(Evaluate(terrain, grid, options));
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(grid.width * grid.height));
}
BENCHMARK(BM_EvaluateGridSphere)
    ->ArgsProduct({{512, 2048}, {1, 0}})
    ->ArgNames({"cells", "threads"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

/// Tiles of 256 x 256 cells at the level given by the argument (0 is 8 m spacing, 5 is 0.25 m),
/// with a warm crater cache, evaluated in parallel across every hardware thread.
void BM_EvaluateTiles(benchmark::State& state) {
    static const Terrain terrain = LunarMare(4096.0, 1.0);
    const TileScheme scheme{{-1024.0, 1024.0}, 2048.0, 256};
    const int level = static_cast<int>(state.range(0));
    std::vector<TileKey> keys;
    for (std::int64_t y = 0; y < 4; ++y) {
        for (std::int64_t x = 0; x < 4; ++x) {
            keys.push_back({level, x, y});
        }
    }
    EvaluateTiles(terrain, scheme, keys);
    for (auto _ : state) {
        benchmark::DoNotOptimize(EvaluateTiles(terrain, scheme, keys));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(keys.size()));
    state.counters["spacing_m"] = scheme.Spacing(level);
}
BENCHMARK(BM_EvaluateTiles)
    ->DenseRange(0, 5)
    ->ArgName("level")
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

}  // namespace

}  // namespace terraingen::benchmarks
