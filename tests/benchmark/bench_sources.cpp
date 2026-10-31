// SPDX-License-Identifier: BSD-3-Clause

#include "fixtures.h"

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace terraingen::benchmarks {

namespace {

/// The upsamplers `BM_FillRasterSource` takes by its first argument: cubic (0), Lanczos3 (1) and
/// diamond-square (2).
std::shared_ptr<const Upsampler> UpsamplerNumbered(std::int64_t n) {
    if (n == 1) {
        return std::make_shared<KernelUpsampler>(ResampleMethod::Lanczos3);
    }
    if (n == 2) {
        return std::make_shared<DiamondSquareUpsampler>();
    }
    return std::make_shared<KernelUpsampler>(ResampleMethod::Cubic);
}

/// A 512 x 512 DEM of 4 m posts, rolling hills, upsampled onto a 2048 x 2048 grid of 1 m cells by
/// the upsampler the first argument numbers (`UpsamplerNumbered`), on the number of threads given
/// by the second (0 for every hardware thread).
void BM_FillRasterSource(benchmark::State& state) {
    Raster<float> dem(RasterGrid{512, 512, GeoTransform::NorthUp(-1024.0, 1024.0, 4.0)});
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            const Vec2d p = dem.CellCenter(col, row);
            dem(col, row) =
                static_cast<float>(30.0 * std::sin(p.x / 170.0) * std::cos(p.y / 130.0) +
                                   2.0 * std::sin(p.x / 9.0 + p.y / 13.0));
        }
    }
    RasterSourceOptions options;
    options.upsampler = UpsamplerNumbered(state.range(0));
    const RasterSource source(std::move(dem), options);
    const RasterGrid grid{2048, 2048, GeoTransform::NorthUp(-1024.0, 1024.0, 1.0)};
    Raster<float> heights(grid);
    const auto threads = static_cast<unsigned>(state.range(1));
    for (auto _ : state) {
        source.Fill(heights, 1.0, threads);
        benchmark::DoNotOptimize(heights.Values().data());
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(grid.width * grid.height));
}
BENCHMARK(BM_FillRasterSource)
    ->ArgsProduct({{0, 1, 2}, {1, 0}})
    ->ArgNames({"upsampler", "threads"})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

}  // namespace

}  // namespace terraingen::benchmarks
