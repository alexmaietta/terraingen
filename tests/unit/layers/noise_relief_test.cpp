// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_relief.h"

#include "support/referenced_source.h"

#include "terraingen/gpu/kernel.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/noise/surface_noise.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::GeoTransform;
using terraingen::NoiseRelief;
using terraingen::Raster;
using terraingen::SyntheticTerrainParams;

namespace {

SyntheticTerrainParams Params() {
    SyntheticTerrainParams params{{terraingen::NoiseLayer{}}};
    params.layers[0].fractal.wavelength = 50.0f;
    params.layers[0].fractal.amplitude = 4.0f;
    return params;
}

}  // namespace

TEST(NoiseRelief, AddsTheSyntheticTerrainAtTheOrigin) {
    Raster<float> heights(24, 16, 10.0f);
    heights.SetTransform(GeoTransform::NorthUp(-24.0, 16.0, 2.0));
    NoiseRelief(Params().layers).Apply(heights, {2.0});
    Raster<float> expected(24, 16);
    expected.SetTransform(heights.Transform());
    terraingen::SyntheticSource(Params()).Fill(expected);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], 10.0f + expected.Values()[i]) << i;
    }
}

TEST(NoiseRelief, MeasuresPositionsFromItsOriginAndSkipsNoData) {
    Raster<float> heights(4, 4, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(500000.0, 4000000.0, 1.0));
    heights.SetNoData(std::numeric_limits<float>::quiet_NaN());
    heights(1, 2) = std::numeric_limits<float>::quiet_NaN();
    const SyntheticTerrainParams params = Params();
    NoiseRelief(params.layers, terraingen::testing::ReferencedBase({500000.0, 4000000.0}))
        .Apply(heights, {1.0});
    EXPECT_TRUE(std::isnan(heights(1, 2)));
    const auto center = heights.CellCenter(3, 1);
    EXPECT_EQ(heights(3, 1),
              terraingen::EvaluateNoiseLayers(static_cast<float>(center.x - 500000.0),
                                              static_cast<float>(center.y - 4000000.0),
                                              params.layers.data(), 1));
}

TEST(NoiseRelief, RejectsInvalidParameters) {
    EXPECT_THROW(NoiseRelief{std::vector<terraingen::NoiseLayer>{}}, std::invalid_argument);
    EXPECT_EQ(NoiseRelief(Params().layers).Options().layers, Params().layers);
}

TEST(NoiseRelief, PointQueriesMatchCellsAndKeepNoData) {
    Raster<float> heights(8, 8, 3.0f);
    heights.SetTransform(GeoTransform::NorthUp(-8.0, 8.0, 2.0));
    const NoiseRelief relief(Params().layers, terraingen::testing::ReferencedBase({1.0, -2.0}));
    relief.Apply(heights, {2.0});
    const auto center = heights.CellCenter(5, 2);
    EXPECT_EQ(relief.ApplyAt(3.0f, center.x, center.y, {2.0}), heights(5, 2));
    EXPECT_TRUE(
        std::isnan(relief.ApplyAt(std::numeric_limits<float>::quiet_NaN(), 0.0, 0.0, {2.0})));
}

TEST(NoiseLayersAt, EvaluatesThePlaneFromItsOriginAndTheSphereIn3D) {
    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 30.0f;
    layer.fractal.amplitude = 2.0f;
    const std::vector<terraingen::NoiseLayer> layers{layer};
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {100.0, 50.0}, terraingen::Surface(), 112.0, 47.0),
              terraingen::EvaluateNoiseLayers(12.0f, -3.0f, layers.data(), layers.size()));
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size()));
}

namespace {

/// Three layers covering warping, blends and the cellular bases.
SyntheticTerrainParams GpuParams() {
    SyntheticTerrainParams params;
    params.layers.assign(3, terraingen::NoiseLayer{});
    params.layers[0].fractal.wavelength = 400.0f;
    params.layers[0].fractal.amplitude = 60.0f;
    params.layers[0].warpAmplitude = 30.0f;
    params.layers[1].fractal.basis = terraingen::NoiseBasis::WorleyF1;
    params.layers[1].fractal.type = terraingen::FractalType::Ridged;
    params.layers[1].fractal.wavelength = 90.0f;
    params.layers[1].fractal.amplitude = 8.0f;
    params.layers[1].fractal.seed = 7u;
    params.layers[2].fractal.basis = terraingen::NoiseBasis::Simplex;
    params.layers[2].fractal.wavelength = 1500.0f;
    params.layers[2].blend = terraingen::LayerBlend::Mask;
    params.layers[2].fractal.seed = 3u;
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

TEST(NoiseRelief, MatchesTheCpuOnEachGpuBackendAndSkipsNoData) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    const NoiseRelief relief(GpuParams().layers,
                             terraingen::testing::ReferencedBase({500000.0, 4000000.0}));
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        for (const float noData : {std::numeric_limits<float>::quiet_NaN(), -9999.0f}) {
            Raster<float> cpu(203, 77, 12.5f);
            cpu.SetTransform(GeoTransform::NorthUp(498000.0, 4001000.0, 5.0));
            cpu.SetNoData(noData);
            cpu(3, 4) = noData;
            cpu(202, 76) = noData;
            Raster<float> gpu = cpu;
            relief.Apply(cpu, {5.0});
            relief.Apply(gpu, On(backend, 5.0));
            ExpectNear(gpu, cpu);
            EXPECT_TRUE(gpu.IsNoData(gpu(3, 4)) && gpu.IsNoData(gpu(202, 76)));
        }
    }
}

TEST(NoiseRelief, GpuGridsAreTheSameWhateverTheTiling) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    const NoiseRelief relief(GpuParams().layers);
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> whole(96, 64, 0.0f);
        whole.SetTransform(GeoTransform::NorthUp(-96.0, 64.0, 2.0));
        relief.Apply(whole, On(backend, 2.0));
        Raster<float> part(23, 29, 0.0f);
        part.SetTransform(GeoTransform::NorthUp(-96.0 + 2.0 * 17, 64.0 - 2.0 * 11, 2.0));
        relief.Apply(part, On(backend, 2.0));
        for (std::size_t row = 0; row < part.Height(); ++row) {
            for (std::size_t col = 0; col < part.Width(); ++col) {
                ASSERT_EQ(part(col, row), whole(17 + col, 11 + row)) << col << "," << row;
            }
        }
    }
}

TEST(NoiseRelief, SplitsLargeGridsIntoStripsOnEachGpuBackend) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    SyntheticTerrainParams params = Params();
    params.layers[0].fractal.octaves = 2;
    const NoiseRelief relief(params.layers);
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> heights(4096, 4097, 1.0f);
        heights.SetTransform(GeoTransform::NorthUp(0.0, 0.0, 1.0));
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

TEST(NoiseRelief, FallsBackToTheCpuOnSpheresAndRotatedOrVeryWideGrids) {
    const std::vector<terraingen::Backend> gpus = GpuBackends();
    if (gpus.empty()) {
        GTEST_SKIP() << "No GPU backend runs here";
    }
    for (const terraingen::Backend backend : gpus) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        const auto expectSame = [&](const NoiseRelief& relief, Raster<float> cpu, double spacing) {
            Raster<float> gpu = cpu;
            relief.Apply(cpu, {spacing});
            relief.Apply(gpu, On(backend, spacing));
            for (std::size_t i = 0; i < cpu.Size(); ++i) {
                ASSERT_EQ(gpu.Values()[i], cpu.Values()[i]) << i;
            }
        };
        Raster<float> lonLat(16, 8, 0.0f);
        lonLat.SetTransform(GeoTransform::NorthUp(10.0, 20.0, 0.01));
        expectSame(NoiseRelief(GpuParams().layers, terraingen::Surface::Sphere(1737400.0)), lonLat,
                   300.0);
        Raster<float> rotated(16, 8, 0.0f);
        rotated.SetTransform(GeoTransform{0.0, 2.0, 0.5, 0.0, 0.5, -2.0});
        expectSame(NoiseRelief(GpuParams().layers), rotated, 2.0);
        Raster<float> wide(16 * 65535 + 1, 1, 0.0f);
        expectSame(NoiseRelief(Params().layers), wide, 1.0);
    }
}

TEST(NoiseRelief, SkipsDisabledLayersAsTheFullStackDoes) {
    SyntheticTerrainParams params = GpuParams();
    params.layers[1].enabled = false;
    const terraingen::NoiseReliefKernel kernel = terraingen::MakeNoiseReliefKernel(params.layers);
    EXPECT_EQ(kernel.layers.count, 2u);
    Raster<float> heights(9, 7, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(30.0, 40.0, 3.0));
    NoiseRelief(params.layers, terraingen::testing::ReferencedBase({10.0, 5.0}))
        .Apply(heights, {3.0});
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(heights(col, row), 2.0f + terraingen::EvaluateNoiseLayers(
                                                    static_cast<float>(center.x - 10.0),
                                                    static_cast<float>(center.y - 5.0),
                                                    params.layers.data(), params.layers.size()))
                << col << "," << row;
        }
    }
}

TEST(NoiseRelief, PointsStayOnTheCpuWhateverTheBackend) {
    const NoiseRelief relief(GpuParams().layers,
                             terraingen::testing::ReferencedBase({-20.0, 15.0}));
    const std::vector<terraingen::Vec2d> points{{1.5, 2.5}, {-300.0, 812.25}};
    std::vector<float> cpu(points.size(), 4.0f);
    std::vector<float> vulkan = cpu;
    terraingen::SampleBlock onCpu(points, cpu);
    terraingen::SampleBlock onVulkan(points, vulkan);
    relief.Apply(onCpu, {2.0});
    relief.Apply(onVulkan, On(terraingen::Backend::Vulkan, 2.0));
    EXPECT_EQ(vulkan, cpu);
}

TEST(NoiseRelief, RunsOnEachGpuBackendItselfAndNotWithoutItsCompiledForm) {
    const terraingen::NoiseReliefKernel kernel = terraingen::MakeNoiseReliefKernel(Params().layers);
    const auto bytes = terraingen::AsBytes(terraingen::Span(&kernel, 1));
    const terraingen::KernelProgram program =
        terraingen::CompiledKernel<terraingen::NoiseReliefKernel>();
    Raster<float> heights(33, 17, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(0.0, 0.0, 2.0));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Cpu, program, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Vulkan, {}, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Cuda, {}, bytes, heights, {}));
    EXPECT_FALSE(terraingen::RunKernel(terraingen::Backend::Metal, {}, bytes, heights, {}));
    EXPECT_TRUE(std::all_of(heights.Values().begin(), heights.Values().end(),
                            [](float h) { return h == 0.0f; }));
    EXPECT_FALSE(program.spirv.empty());
    for (const terraingen::Backend backend : GpuBackends()) {
        SCOPED_TRACE(std::string(terraingen::BackendName(backend)));
        Raster<float> gpu(33, 17, 0.0f);
        gpu.SetTransform(heights.Transform());
        ASSERT_TRUE(terraingen::RunKernel(backend, program, bytes, gpu, {}));
        EXPECT_NE(gpu(5, 5), 0.0f);
    }
}

TEST(NoiseRelief, TakesOneLayerAlone) {
    const terraingen::NoiseLayer layer = terraingen::NoiseLayer::Fbm(3, 800.0, 40.0);
    EXPECT_EQ(terraingen::NoiseRelief(layer).Options().layers,
              std::vector<terraingen::NoiseLayer>{layer});
}
