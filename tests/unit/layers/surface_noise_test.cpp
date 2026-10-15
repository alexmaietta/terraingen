#include "terraingen/layers/surface_noise.h"
#include "terraingen/noise/kernels/perlin.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using terraingen::Surface;

TEST(PatchNoise, StaysInRangeAndFollowsTwoOctavesOfPerlinNoise) {
    const Surface plane;
    double lowest = 1.0;
    double highest = -1.0;
    for (int i = 0; i < 400; ++i) {
        const double x = 7.3 * i;
        const double y = -3.1 * i;
        const double value = terraingen::PatchNoise(plane, x, y, 50.0, 9u);
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        const auto px = static_cast<float>(x / 50.0);
        const auto py = static_cast<float>(y / 50.0);
        const auto coarse = static_cast<double>(terraingen::PerlinNoise2(px, py, 9u));
        const auto fine = static_cast<double>(terraingen::PerlinNoise2(4.0f * px, 4.0f * py, 10u));
        const double expected = 2.5 * (0.7 * coarse + 0.3 * fine);
        EXPECT_EQ(value, std::clamp(expected, -1.0, 1.0));
    }
    EXPECT_GE(lowest, -1.0);
    EXPECT_LE(highest, 1.0);
    EXPECT_LT(lowest, -0.5) << "patches reach both ends";
    EXPECT_GT(highest, 0.5);
}

TEST(PatchNoise, IsSmoothNoiseOnASphere) {
    const Surface moon = Surface::Sphere(1737400.0);
    const double here = terraingen::PatchNoise(moon, 10.0, 20.0, 1000.0, 3u);
    const double near = terraingen::PatchNoise(moon, 10.0 + 1e-6, 20.0, 1000.0, 3u);
    EXPECT_GE(here, -1.0);
    EXPECT_LE(here, 1.0);
    EXPECT_NEAR(here, near, 1e-3);
    EXPECT_NE(terraingen::PatchNoise(moon, 10.0, 20.0, 1000.0, 4u), here);
}

TEST(NoiseLayersAt, EvaluatesThePlaneFromItsOriginAndTheSphereIn3D) {
    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 30.0f;
    layer.fractal.amplitude = 2.0f;
    const std::vector<terraingen::NoiseLayer> layers{layer};
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {100.0, 50.0}, Surface(), 112.0, 47.0),
              terraingen::EvaluateNoiseLayers(12.0f, -3.0f, layers.data(), layers.size()));
    const Surface moon = Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size()));
}
