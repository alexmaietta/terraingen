#include "fixtures.h"

#include <benchmark/benchmark.h>

namespace terraingen::benchmarks {

namespace {

/// Lunar craters from 2.5 m to 200 m generated over a square region whose side, in metres, is the
/// argument.
void BM_GenerateCraters(benchmark::State& state) {
    const auto density = testing::LunarCraterDensity(3.5);
    const auto ages = testing::LunarCraterAges(3.5);
    const double half = 0.5 * static_cast<double>(state.range(0));
    const Bounds2d region{-half, -half, half, half};
    std::size_t craters = 0;
    for (auto _ : state) {
        CraterList list = GenerateCraters(PlanarCells(), region, *density, *ages, 1u,
                                          {.dMin = 2.5, .dMax = 200.0});
        craters = list.Size();
        benchmark::DoNotOptimize(list);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(craters));
    state.counters["craters"] = static_cast<double>(craters);
}
BENCHMARK(BM_GenerateCraters)->RangeMultiplier(4)->Range(256, 4096)->Unit(benchmark::kMillisecond);

/// Those craters stamped onto a 1 m grid of the same region on the number of threads given by the
/// second argument (0 for every hardware thread).
void BM_ApplyCraters(benchmark::State& state) {
    const auto density = testing::LunarCraterDensity(3.5);
    const auto ages = testing::LunarCraterAges(3.5);
    const auto side = static_cast<std::size_t>(state.range(0));
    const double half = 0.5 * static_cast<double>(side);
    const CraterList craters =
        GenerateCraters(PlanarCells(), {-half, -half, half, half}, *density, *ages, 1u,
                        {.dMin = 2.5, .dMax = 200.0, .marginRadii = 1.5});
    const terraingen::CraterShapeModels models{
        std::make_shared<FassettThomsonProfile>(),
        std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry)};
    Raster<float> heights(side, side, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(-half, half, 1.0));
    for (auto _ : state) {
        ApplyCraters(heights, craters, models, {}, {0.0, static_cast<unsigned>(state.range(1))});
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(craters.Size()));
}
BENCHMARK(BM_ApplyCraters)
    ->ArgsProduct({{1024}, {1, 0}})
    ->ArgNames({"cells", "threads"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

}  // namespace

}  // namespace terraingen::benchmarks

BENCHMARK_MAIN();
