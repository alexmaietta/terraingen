// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/validate.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::Fractal;
using terraingen::FractalBlend;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::NoiseBasis;
using terraingen::ValidateFractal;
using terraingen::ValidateFractalParams;
using terraingen::ValidateFractals;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

}  // namespace

TEST(ValidateFractalParams, AcceptsDefaults) {
    EXPECT_NO_THROW(ValidateFractalParams(FractalParams{}));
}

TEST(ValidateFractalParams, RejectsInvalidFields) {
    const auto rejects = [](auto mutate) {
        FractalParams params;
        mutate(params);
        EXPECT_THROW(ValidateFractalParams(params), std::invalid_argument);
    };
    rejects([](FractalParams& p) { p.wavelength = 0.0f; });
    rejects([](FractalParams& p) { p.wavelength = kInf; });
    rejects([](FractalParams& p) { p.amplitude = kNaN; });
    rejects([](FractalParams& p) { p.octaves = 0; });
    rejects([](FractalParams& p) { p.octaves = 33; });
    rejects([](FractalParams& p) { p.lacunarity = 1.0f; });
    rejects([](FractalParams& p) { p.lacunarity = kInf; });
    rejects([](FractalParams& p) { p.gain = 0.0f; });
    rejects([](FractalParams& p) { p.gain = kNaN; });
    rejects([](FractalParams& p) { p.basis = static_cast<NoiseBasis>(99); });
    rejects([](FractalParams& p) { p.type = static_cast<FractalType>(99); });
    rejects([](FractalParams& p) { p.offset = 0.0f; });
    rejects([](FractalParams& p) { p.offset = kInf; });
}

TEST(ValidateFractal, RejectsInvalidBlendAndWarp) {
    const auto rejects = [](auto mutate) {
        Fractal layer;
        mutate(layer);
        EXPECT_THROW(ValidateFractal(layer), std::invalid_argument);
    };
    rejects([](Fractal& l) { l.octaves = 0; });
    rejects([](Fractal& l) { l.blend = static_cast<FractalBlend>(99); });
    rejects([](Fractal& l) { l.warpAmplitude = -1.0f; });
    rejects([](Fractal& l) { l.warpAmplitude = kNaN; });
    rejects([](Fractal& l) { l.warpWavelength = 0.0f; });
    EXPECT_NO_THROW(ValidateFractal(Fractal{}));
}

TEST(ValidateFractals, ChecksTheCountAndNamesTheBadLayer) {
    EXPECT_NO_THROW(ValidateFractals({}));
    EXPECT_THROW(ValidateFractals({}, 1), std::invalid_argument);
    EXPECT_THROW(ValidateFractals(std::vector<Fractal>(terraingen::kMaxFractals + 1)),
                 std::invalid_argument);
    std::vector<Fractal> layers(2);
    layers[1].gain = 0.0f;
    try {
        ValidateFractals(layers);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()).rfind("Layer 2:", 0), 0u) << error.what();
    }
}
