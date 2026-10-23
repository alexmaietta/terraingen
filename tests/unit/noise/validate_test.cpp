#include "terraingen/noise/validate.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::LayerBlend;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::ValidateFractalParams;
using terraingen::ValidateNoiseLayer;
using terraingen::ValidateNoiseLayers;

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

TEST(ValidateNoiseLayer, RejectsInvalidBlendAndWarp) {
    const auto rejects = [](auto mutate) {
        NoiseLayer layer;
        mutate(layer);
        EXPECT_THROW(ValidateNoiseLayer(layer), std::invalid_argument);
    };
    rejects([](NoiseLayer& l) { l.fractal.octaves = 0; });
    rejects([](NoiseLayer& l) { l.blend = static_cast<LayerBlend>(99); });
    rejects([](NoiseLayer& l) { l.warpAmplitude = -1.0f; });
    rejects([](NoiseLayer& l) { l.warpAmplitude = kNaN; });
    rejects([](NoiseLayer& l) { l.warpWavelength = 0.0f; });
    EXPECT_NO_THROW(ValidateNoiseLayer(NoiseLayer{}));
}

TEST(ValidateNoiseLayers, ChecksTheCountAndNamesTheBadLayer) {
    EXPECT_NO_THROW(ValidateNoiseLayers({}));
    EXPECT_THROW(ValidateNoiseLayers({}, 1), std::invalid_argument);
    EXPECT_THROW(ValidateNoiseLayers(std::vector<NoiseLayer>(terraingen::kMaxNoiseLayers + 1)),
                 std::invalid_argument);
    std::vector<NoiseLayer> layers(2);
    layers[1].fractal.gain = 0.0f;
    try {
        ValidateNoiseLayers(layers);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()).rfind("Layer 2:", 0), 0u) << error.what();
    }
}
