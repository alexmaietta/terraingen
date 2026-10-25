// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/surface_noise.h"

#include <gtest/gtest.h>

#include <vector>

using terraingen::Fractal;
using terraingen::FractalStackAt;
using terraingen::Surface;

TEST(FractalStackAt, EvaluatesRelativeToTheOriginOnAPlane) {
    std::vector<Fractal> layers(1);
    layers[0].wavelength = 20.0f;
    EXPECT_EQ(FractalStackAt(layers, {1000.0, -500.0}, Surface{}, 1003.5, -492.25),
              terraingen::EvaluateFractals(3.5f, 7.75f, layers.data(), layers.size()));
    EXPECT_EQ(FractalStackAt({}, {}, Surface{}, 3.0, 4.0), 0.0f);
}

TEST(FractalStackAt, EvaluatesAtThe3dPositionOnABody) {
    std::vector<Fractal> layers(1);
    layers[0].wavelength = 5000.0f;
    const Surface moon = Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(FractalStackAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateFractals3(p.x, p.y, p.z, layers.data(), layers.size()));
    EXPECT_EQ(FractalStackAt({}, {}, moon, 10.0, 20.0), 0.0f);
}
