#include "terraingen/gpu/vulkan.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/random/philox.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
extern const unsigned char terraingen_test_noise_conformance[];
extern const std::size_t terraingen_test_noise_conformance_size;
}

using terraingen::FractalType;
using terraingen::LayerBlend;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::NoiseLayers;

namespace {

constexpr std::uint32_t kValues = 17;

static_assert(sizeof(NoiseLayer) == 52 && sizeof(NoiseLayers) == 420,
              "NoiseLayers must match its std430 layout in GPU kernels");

/// Eight layers covering every basis, fractal type, blend mode and the domain warp.
std::vector<NoiseLayer> TestLayers() {
    std::vector<NoiseLayer> layers(terraingen::kMaxNoiseLayers);
    const NoiseBasis bases[] = {
        NoiseBasis::Perlin,   NoiseBasis::Simplex,         NoiseBasis::Value,  NoiseBasis::WorleyF1,
        NoiseBasis::WorleyF2, NoiseBasis::WorleyF2MinusF1, NoiseBasis::Perlin, NoiseBasis::Simplex};
    const FractalType types[] = {FractalType::Fbm, FractalType::Ridged, FractalType::Billow,
                                 FractalType::HybridMultifractal};
    const LayerBlend blends[] = {LayerBlend::Add, LayerBlend::Max, LayerBlend::Min,
                                 LayerBlend::Mask};
    for (std::size_t j = 0; j < layers.size(); ++j) {
        NoiseLayer& layer = layers[j];
        layer.enabled = j != 5;
        layer.fractal.seed = 11u + static_cast<std::uint32_t>(j);
        layer.fractal.wavelength = 300.0f + 150.0f * static_cast<float>(j);
        layer.fractal.amplitude = 5.0f + static_cast<float>(j);
        layer.fractal.octaves = 3 + static_cast<int>(j % 4);
        layer.fractal.offset = j == 1 ? 0.8f : 1.0f;
        layer.fractal.basis = bases[j];
        layer.fractal.type = types[j % 4];
        layer.blend = blends[j % 4];
        layer.warpAmplitude = j == 6 ? 40.0f : 0.0f;
        layer.warpWavelength = 700.0f;
    }
    return layers;
}

/// `layers` as the bytes of a `NoiseLayers` block, with every padding byte zero so that each
/// `bool` reads back as 0 or 1 on the GPU.
std::vector<std::byte> Upload(const std::vector<NoiseLayer>& layers) {
    std::vector<std::byte> bytes(sizeof(NoiseLayers));
    const auto put = [&](std::size_t offset, const auto& value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    for (std::size_t j = 0; j < layers.size(); ++j) {
        const std::size_t at = offsetof(NoiseLayers, layers) + j * sizeof(NoiseLayer);
        put(at + offsetof(NoiseLayer, enabled), layers[j].enabled);
        put(at + offsetof(NoiseLayer, fractal), layers[j].fractal);
        put(at + offsetof(NoiseLayer, blend), layers[j].blend);
        put(at + offsetof(NoiseLayer, warpAmplitude), layers[j].warpAmplitude);
        put(at + offsetof(NoiseLayer, warpWavelength), layers[j].warpWavelength);
    }
    put(offsetof(NoiseLayers, count), static_cast<std::uint32_t>(layers.size()));
    return bytes;
}

}  // namespace

TEST(GpuNoise, MatchesTheCpuWithinTolerancesAndPhiloxExactly) {
    terraingen::VulkanCompute* compute = terraingen::VulkanCompute::Get();
    if (compute == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    const std::uint32_t count = 4096;
    const std::uint32_t seed = 977u;
    std::vector<float> points(3u * count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i);
        points[3u * i + 0u] = -60.0f + 0.0291f * t;
        points[3u * i + 1u] = 37.0f - 0.0173f * t + 3.0f * std::sin(0.01f * t);
        points[3u * i + 2u] = 0.011f * t - 20.0f;
    }
    const std::vector<NoiseLayer> layers = TestLayers();
    std::vector<std::byte> block = Upload(layers);
    std::vector<float> values(kValues * count, 0.0f);
    std::vector<std::uint32_t> words(4u * count, 0u);
    struct {
        std::uint32_t count;
        std::uint32_t seed;
    } push{count, seed};
    compute->Run({terraingen_test_noise_conformance, terraingen_test_noise_conformance_size},
                 {std::as_writable_bytes(std::span(points)), std::span(block),
                  std::as_writable_bytes(std::span(values)),
                  std::as_writable_bytes(std::span(words))},
                 std::as_bytes(std::span(&push, 1)), {(count + 63u) / 64u, 1u, 1u});

    NoiseLayers cpuBlock;
    for (const NoiseLayer& layer : layers) {
        cpuBlock.layers[cpuBlock.count++] = layer;
    }
    std::vector<double> worst(kValues, 0.0);
    for (std::uint32_t i = 0; i < count; ++i) {
        const float x = points[3u * i + 0u];
        const float y = points[3u * i + 1u];
        const float z = points[3u * i + 2u];
        const terraingen::WorleyDistances worley = terraingen::Worley2(x, y, seed);
        float expected[kValues] = {
            terraingen::PerlinNoise2(x, y, seed),
            terraingen::SimplexNoise2(x, y, seed),
            terraingen::ValueNoise2(x, y, seed),
            worley.f1,
            worley.f2,
            terraingen::PerlinNoise3(x, y, z, seed),
            terraingen::BasisNoise3(NoiseBasis::Value, terraingen::SplitLattice3(x, y, z), seed)};
        for (std::uint32_t j = 0; j < terraingen::kMaxNoiseLayers; ++j) {
            expected[7u + j] = terraingen::EvaluateNoiseLayer(100.0f * x, 100.0f * y, layers[j]);
        }
        expected[15] = terraingen::EvaluateNoiseLayers(100.0f * x, 100.0f * y, cpuBlock);
        expected[16] =
            terraingen::HashToUnitFloat(terraingen::PcgHash(std::bit_cast<std::uint32_t>(x)));
        for (std::uint32_t k = 0; k < kValues; ++k) {
            const double scale = std::max(1.0, std::fabs(static_cast<double>(expected[k])));
            worst[k] = std::max(
                worst[k],
                std::fabs(static_cast<double>(values[kValues * i + k] - expected[k])) / scale);
        }
        const terraingen::U32x4 philox = terraingen::Philox4x32(
            {i, std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y), seed},
            {seed, ~i});
        ASSERT_EQ(words[4u * i + 0u], philox.x) << i;
        ASSERT_EQ(words[4u * i + 1u], philox.y) << i;
        ASSERT_EQ(words[4u * i + 2u], philox.z) << i;
        ASSERT_EQ(words[4u * i + 3u], philox.w) << i;
    }
    for (std::uint32_t k = 0; k < kValues; ++k) {
        EXPECT_LT(worst[k], 1e-4) << "value " << k << " on " << compute->DeviceName();
    }
    EXPECT_EQ(worst[16], 0.0);
}
