// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_layer.h"

#include "support/referenced_source.h"

#include "gpu/kernel.h"
#include "terraingen/noise/fractals.h"
#include "terraingen/noise/surface_noise.h"
#include "terraingen/random/stream.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::GeoTransform;
using terraingen::NoiseLayer;
using terraingen::Raster;
using terraingen::RasterGrid;

namespace {

std::vector<terraingen::Fractal> Params() {
    std::vector<terraingen::Fractal> params{terraingen::Fractal{}};
    params[0].wavelength = 50.0f;
    params[0].amplitude = 4.0f;
    return params;
}

/// The context at `spacing` on `backend` over a source whose reference point is `origin`, which
/// it keeps.
struct Measured {
    explicit Measured(terraingen::Vec2d origin) : source(origin) {}

    terraingen::LayerContext At(double spacing,
                                terraingen::Backend backend = terraingen::Backend::Cpu) const {
        terraingen::LayerContext context{spacing, 1, nullptr, backend};
        context.source = &source;
        return context;
    }

    terraingen::testing::ReferencedSource source;
};

}  // namespace

TEST(NoiseLayer, AddsTheSyntheticTerrainAtTheOrigin) {
    Raster<float> heights(RasterGrid{24, 16, GeoTransform::NorthUp(-24.0, 16.0, 2.0)}, 10.0f);
    NoiseLayer(Params()).Apply(heights, {2.0});
    Raster<float> expected(RasterGrid{24, 16, heights.Transform()});
    terraingen::SyntheticSource(Params()).Fill(expected);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], 10.0f + expected.Values()[i]) << i;
    }
}

TEST(NoiseLayer, MeasuresPositionsFromItsOriginAndSkipsNoData) {
    Raster<float> heights(RasterGrid{4, 4, GeoTransform::NorthUp(500000.0, 4000000.0, 1.0)}, 0.0f);
    heights.SetNoData(std::numeric_limits<float>::quiet_NaN());
    heights(1, 2) = std::numeric_limits<float>::quiet_NaN();
    const std::vector<terraingen::Fractal> params = Params();
    NoiseLayer(params).Apply(heights, Measured({500000.0, 4000000.0}).At(1.0));
    EXPECT_TRUE(std::isnan(heights(1, 2)));
    const auto center = heights.CellCenter(3, 1);
    EXPECT_EQ(heights(3, 1), terraingen::EvaluateFractals(static_cast<float>(center.x - 500000.0),
                                                          static_cast<float>(center.y - 4000000.0),
                                                          params.data(), 1));
}

TEST(NoiseLayer, RejectsInvalidParameters) {
    EXPECT_THROW(NoiseLayer{std::vector<terraingen::Fractal>{}}, std::invalid_argument);
    EXPECT_EQ(NoiseLayer(Params()).Options().fractals, Params());
}

TEST(NoiseLayer, PointQueriesMatchCellsAndKeepNoData) {
    Raster<float> heights(RasterGrid{8, 8, GeoTransform::NorthUp(-8.0, 8.0, 2.0)}, 3.0f);
    const NoiseLayer relief(Params());
    const Measured from({1.0, -2.0});
    relief.Apply(heights, from.At(2.0));
    const auto center = heights.CellCenter(5, 2);
    EXPECT_EQ(relief.ApplyAt(3.0f, center.x, center.y, from.At(2.0)), heights(5, 2));
    EXPECT_NE(relief.ApplyAt(3.0f, center.x, center.y, {2.0}), heights(5, 2))
        << "measured from the origin without the source";
    EXPECT_TRUE(std::isnan(
        relief.ApplyAt(std::numeric_limits<float>::quiet_NaN(), 0.0, 0.0, from.At(2.0))));
}

TEST(NoiseLayer, ReseedsEachFractalFromItsOwnWord) {
    std::vector<terraingen::Fractal> params = Params();
    params.push_back(params[0]);
    params[1].wavelength = 9.0f;
    const NoiseLayer relief(params);
    const terraingen::LayerPtr held = relief.Reseeded(11);
    const auto& reseeded = dynamic_cast<const NoiseLayer&>(*held);
    for (std::size_t n = 0; n < reseeded.Options().fractals.size(); ++n) {
        EXPECT_EQ(reseeded.Options().fractals[n].seed,
                  terraingen::ReseedWord(11, static_cast<std::uint32_t>(n)));
    }
    const float a = reseeded.ApplyAt(0.0f, 3.0, 4.0, {1.0});
    EXPECT_EQ(a, relief.Reseeded(11)->ApplyAt(0.0f, 3.0, 4.0, {1.0}));
    EXPECT_NE(a, relief.Reseeded(12)->ApplyAt(0.0f, 3.0, 4.0, {1.0}));
}

TEST(FractalStackAt, EvaluatesThePlaneFromItsOriginAndTheSphereIn3D) {
    terraingen::Fractal layer;
    layer.wavelength = 30.0f;
    layer.amplitude = 2.0f;
    const std::vector<terraingen::Fractal> layers{layer};
    EXPECT_EQ(terraingen::FractalStackAt(layers, {100.0, 50.0}, terraingen::Surface(), 112.0, 47.0),
              terraingen::EvaluateFractals(12.0f, -3.0f, layers.data(), layers.size()));
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(terraingen::FractalStackAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateFractals3(p.x, p.y, p.z, layers.data(), layers.size()));
}

namespace {

/// Three layers covering warping, blends and the cellular bases.
std::vector<terraingen::Fractal> GpuParams() {
    std::vector<terraingen::Fractal> params;
    params.assign(3, terraingen::Fractal{});
    params[0].wavelength = 400.0f;
    params[0].amplitude = 60.0f;
    params[0].warpAmplitude = 30.0f;
    params[1].basis = terraingen::NoiseBasis::WorleyF1;
    params[1].type = terraingen::FractalType::Ridged;
    params[1].wavelength = 90.0f;
    params[1].amplitude = 8.0f;
    params[1].seed = 7u;
    params[2].basis = terraingen::NoiseBasis::Simplex;
    params[2].wavelength = 1500.0f;
    params[2].blend = terraingen::FractalBlend::Mask;
    params[2].seed = 3u;
    return params;
}

/// The GPU backends that run here.
std::vector<terraingen::Backend> GpuBackends() {
    std::vector<terraingen::Backend> gpus;
    for (const terraingen::BackendStatus& status : terraingen::Backends()) {
        if (status.available && status.backend != terraingen::Backend::Cpu) {
            gpus.push_back(status.backend);
        }
    }
    return gpus;
}

terraingen::LayerContext On(terraingen::Backend backend, double spacing) {
    return {spacing, 1, nullptr, backend};
}

/// Expects `gpu` to equal `cpu` to within float rounding, with the same no-data cells.
void ExpectNear(const Raster<float>& gpu, const Raster<float>& cpu) {
    ASSERT_EQ(gpu.Size(), cpu.Size());
    for (std::size_t i = 0; i < gpu.Size(); ++i) {
        const float expected = cpu.Values()[i];
        if (std::isnan(expected)) {
            ASSERT_TRUE(std::isnan(gpu.Values()[i])) << i;
            continue;
        }
        ASSERT_NEAR(gpu.Values()[i], expected, 1e-4f * std::max(1.0f, std::fabs(expected))) << i;
    }
}

}  // namespace

TEST(NoiseLayer, MatchesTheCpuOnEachGpuBackendAndSkipsNoData) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    const NoiseLayer relief(GpuParams());
    const Measured from({500000.0, 4000000.0});
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        for (const float noData : {std::numeric_limits<float>::quiet_NaN(), -9999.0f}) {
            Raster<float> cpu(RasterGrid{203, 77, GeoTransform::NorthUp(498000.0, 4001000.0, 5.0)},
                              12.5f);
            cpu.SetNoData(noData);
            cpu(3, 4) = noData;
            cpu(202, 76) = noData;
            Raster<float> gpu = cpu;
            relief.Apply(cpu, from.At(5.0));
            relief.Apply(gpu, from.At(5.0, backend));
            ExpectNear(gpu, cpu);
            EXPECT_TRUE(gpu.IsNoData(gpu(3, 4)) && gpu.IsNoData(gpu(202, 76)));
        }
    }
}

TEST(NoiseLayer, GpuGridsAreTheSameWhateverTheTiling) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    const NoiseLayer relief(GpuParams());
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> whole(RasterGrid{96, 64, GeoTransform::NorthUp(-96.0, 64.0, 2.0)}, 0.0f);
        relief.Apply(whole, On(backend, 2.0));
        Raster<float> part(
            RasterGrid{23, 29, GeoTransform::NorthUp(-96.0 + 2.0 * 17, 64.0 - 2.0 * 11, 2.0)},
            0.0f);
        relief.Apply(part, On(backend, 2.0));
        for (std::size_t row = 0; row < part.Height(); ++row) {
            for (std::size_t col = 0; col < part.Width(); ++col) {
                ASSERT_EQ(part(col, row), whole(17 + col, 11 + row)) << col << "," << row;
            }
        }
    }
}

TEST(NoiseLayer, SplitsLargeGridsIntoStripsOnEachGpuBackend) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    std::vector<terraingen::Fractal> params = Params();
    params[0].octaves = 2;
    const NoiseLayer relief(params);
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> heights(RasterGrid{4096, 4097, GeoTransform::NorthUp(0.0, 0.0, 1.0)}, 1.0f);
        relief.Apply(heights, On(backend, 1.0));
        for (const std::size_t row : {std::size_t{0}, std::size_t{4095}, std::size_t{4096}}) {
            for (const std::size_t col : {std::size_t{0}, std::size_t{1234}, std::size_t{4095}}) {
                const auto center = heights.CellCenter(col, row);
                const float expected = relief.ApplyAt(1.0f, center.x, center.y, {1.0});
                ASSERT_NEAR(heights(col, row), expected,
                            1e-4f * std::max(1.0f, std::fabs(expected)))
                    << col << "," << row;
            }
        }
    }
}

TEST(NoiseLayer, FallsBackToTheCpuOnSpheresAndRotatedOrVeryWideGrids) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        const auto expectSame = [&](const NoiseLayer& relief, Raster<float> cpu, double spacing,
                                    const terraingen::Surface* surface) {
            Raster<float> gpu = cpu;
            terraingen::LayerContext onCpu{spacing};
            onCpu.surface = surface;
            terraingen::LayerContext onGpu = On(backend, spacing);
            onGpu.surface = surface;
            relief.Apply(cpu, onCpu);
            relief.Apply(gpu, onGpu);
            for (std::size_t i = 0; i < cpu.Size(); ++i) {
                ASSERT_EQ(gpu.Values()[i], cpu.Values()[i]) << i;
            }
        };
        const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
        Raster<float> lonLat(RasterGrid{16, 8, GeoTransform::NorthUp(10.0, 20.0, 0.01)}, 0.0f);
        expectSame(NoiseLayer(GpuParams()), lonLat, 300.0, &moon);
        Raster<float> rotated(RasterGrid{16, 8, GeoTransform{0.0, 2.0, 0.5, 0.0, 0.5, -2.0}}, 0.0f);
        expectSame(NoiseLayer(GpuParams()), rotated, 2.0, nullptr);
        Raster<float> wide(16 * 65535 + 1, 1, 0.0f);
        expectSame(NoiseLayer(Params()), wide, 1.0, nullptr);
    }
}

TEST(NoiseLayer, SkipsDisabledLayersAsTheFullStackDoes) {
    std::vector<terraingen::Fractal> params = GpuParams();
    params[1].enabled = false;
    const terraingen::NoiseLayerKernel kernel = terraingen::MakeNoiseLayerKernel(params);
    EXPECT_EQ(kernel.stack.count, 2u);
    Raster<float> heights(RasterGrid{9, 7, GeoTransform::NorthUp(30.0, 40.0, 3.0)}, 2.0f);
    NoiseLayer(params).Apply(heights, Measured({10.0, 5.0}).At(3.0));
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(heights(col, row),
                      2.0f + terraingen::EvaluateFractals(static_cast<float>(center.x - 10.0),
                                                          static_cast<float>(center.y - 5.0),
                                                          params.data(), params.size()))
                << col << "," << row;
        }
    }
}

TEST(NoiseLayer, PointsStayOnTheCpuWhateverTheBackend) {
    const NoiseLayer relief(GpuParams());
    const Measured from({-20.0, 15.0});
    const std::vector<terraingen::Vec2d> points{{1.5, 2.5}, {-300.0, 812.25}};
    std::vector<float> cpu(points.size(), 4.0f);
    std::vector<float> vulkan = cpu;
    terraingen::SampleBlock onCpu(points, cpu);
    terraingen::SampleBlock onVulkan(points, vulkan);
    relief.Apply(onCpu, from.At(2.0));
    relief.Apply(onVulkan, from.At(2.0, terraingen::Backend::Vulkan));
    EXPECT_EQ(vulkan, cpu);
}

TEST(NoiseLayer, RunsOnEachGpuBackendItselfAndNotWithoutItsCompiledForm) {
    const terraingen::NoiseLayerKernel kernel = terraingen::MakeNoiseLayerKernel(Params());
    const auto bytes = terraingen::AsBytes(terraingen::Span(&kernel, 1));
    const terraingen::KernelProgram program =
        terraingen::CompiledKernel<terraingen::NoiseLayerKernel>();
    Raster<float> heights(RasterGrid{33, 17, GeoTransform::NorthUp(0.0, 0.0, 2.0)}, 0.0f);
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Cpu, program, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Vulkan, {}, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Cuda, {}, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Metal, {}, bytes, heights, {}));
    EXPECT_TRUE(std::all_of(heights.Values().begin(), heights.Values().end(),
                            [](float h) { return h == 0.0f; }));
    EXPECT_FALSE(program.spirv.empty());
    for (const terraingen::Backend backend : GpuBackends()) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> gpu(RasterGrid{33, 17, heights.Transform()}, 0.0f);
        ASSERT_TRUE(terraingen::RunKernel(backend, program, bytes, gpu, {}));
        EXPECT_NE(gpu(5, 5), 0.0f);
    }
}

TEST(NoiseLayer, TakesOneLayerAlone) {
    const terraingen::Fractal layer = terraingen::Fractal::Fbm(3, 800.0, 40.0);
    EXPECT_EQ(terraingen::NoiseLayer(layer).Options().fractals,
              std::vector<terraingen::Fractal>{layer});
}
