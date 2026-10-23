// SPDX-License-Identifier: BSD-3-Clause

#include "fixtures.h"

#include <benchmark/benchmark.h>

namespace terraingen::benchmarks {

namespace {

/// A whole grid (side cells given by the first argument, 1 m cells) on the number of threads given
/// by the second (0 for every hardware thread).
void BM_EvaluateGrid(benchmark::State& state) {
    const auto side = static_cast<double>(state.range(0));
    const TerrainGenerator terrain = LunarMare(side, 1.0);
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
    const TerrainGenerator terrain =
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

/// A synthetic terrain of three 8-octave noise layers, one warped, over a flat source (side cells
/// given by the first argument, 1 m cells) on the backend given by the second: every hardware
/// thread (0), Vulkan (1), CUDA (2) or Metal (3), the order `Backends()` lists them in.
void BM_EvaluateNoiseGrid(benchmark::State& state) {
    const auto cells = static_cast<std::size_t>(state.range(0));
    const BackendStatus status = Backends()[static_cast<std::size_t>(state.range(1))];
    if (!status.available) {
        state.SkipWithError(status.reason.c_str());
        return;
    }
    const Backend backend = status.backend;
    SyntheticTerrainParams noise;
    noise.layers.assign(3, NoiseLayer{});
    noise.layers[0].fractal.wavelength = 2000.0f;
    noise.layers[0].fractal.amplitude = 80.0f;
    noise.layers[0].warpAmplitude = 150.0f;
    noise.layers[1].fractal.type = FractalType::Ridged;
    noise.layers[1].fractal.wavelength = 400.0f;
    noise.layers[1].fractal.amplitude = 20.0f;
    noise.layers[2].fractal.basis = NoiseBasis::Simplex;
    noise.layers[2].fractal.wavelength = 60.0f;
    noise.layers[2].fractal.amplitude = 2.0f;
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(cells, cells, 1.0);
    terrain.layers.push_back(std::make_shared<NoiseRelief>(noise.layers));
    const RasterGrid grid = terrain.source->Grid();
    const EvaluationOptions options{0.0, 0, backend};
    for (auto _ : state) {
        benchmark::DoNotOptimize(Evaluate(terrain, grid, options));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(cells * cells));
}
BENCHMARK(BM_EvaluateNoiseGrid)
    ->ArgsProduct({{1024, 4096}, {0, 1, 2, 3}})
    ->ArgNames({"cells", "backend"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

}  // namespace

}  // namespace terraingen::benchmarks
