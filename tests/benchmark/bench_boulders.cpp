// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_shape.h"

#include <benchmark/benchmark.h>

#include <cstdint>

namespace terraingen::benchmarks {

namespace {

/// One procedural boulder shape of the default styles subdivided as many times as the argument,
/// cycling through 32 seeds so every run times the same mix of styles.
void BM_GenerateBoulderShape(benchmark::State& state) {
    BoulderShapeOptions options;
    options.subdivisions = static_cast<int>(state.range(0));
    std::uint64_t seed = 0;
    for (auto _ : state) {
        BoulderShape shape = GenerateBoulderShape(1 + seed++ % 32, options);
        benchmark::DoNotOptimize(shape);
    }
}
BENCHMARK(BM_GenerateBoulderShape)->DenseRange(3, 5)->Unit(benchmark::kMillisecond);

}  // namespace

}  // namespace terraingen::benchmarks
