// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/surface_noise.h"

#include <gtest/gtest.h>

#include <vector>

using terraingen::NoiseLayer;
using terraingen::NoiseLayersAt;
using terraingen::Surface;

TEST(NoiseLayersAt, EvaluatesRelativeToTheOriginOnAPlane) {
    std::vector<NoiseLayer> layers(1);
    layers[0].fractal.wavelength = 20.0f;
    EXPECT_EQ(NoiseLayersAt(layers, {1000.0, -500.0}, Surface{}, 1003.5, -492.25),
              terraingen::EvaluateNoiseLayers(3.5f, 7.75f, layers.data(), layers.size()));
    EXPECT_EQ(NoiseLayersAt({}, {}, Surface{}, 3.0, 4.0), 0.0f);
}

TEST(NoiseLayersAt, EvaluatesAtThe3dPositionOnABody) {
    std::vector<NoiseLayer> layers(1);
    layers[0].fractal.wavelength = 5000.0f;
    const Surface moon = Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(NoiseLayersAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size()));
    EXPECT_EQ(NoiseLayersAt({}, {}, moon, 10.0, 20.0), 0.0f);
}
