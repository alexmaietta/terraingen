#include "support/log_capture.h"
#include "terraingen/surface/synthetic_terrain.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

using terraingen::EvaluateNoiseLayers;
using terraingen::FillSyntheticTerrain;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::GenerateSyntheticTerrain;
using terraingen::GeoTransform;
using terraingen::kMaxNoiseLayers;
using terraingen::LayerBlend;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::Raster;
using terraingen::SyntheticTerrainParams;
using terraingen::ValidateFractalParams;
using terraingen::ValidateNoiseLayer;
using terraingen::ValidateSyntheticTerrain;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

SyntheticTerrainParams TwoLayers() {
    SyntheticTerrainParams params;
    params.layers[0].fractal.wavelength = 40.0f;
    NoiseLayer detail;
    detail.fractal.seed = 5u;
    detail.fractal.basis = NoiseBasis::Simplex;
    detail.fractal.type = FractalType::Ridged;
    detail.fractal.wavelength = 12.0f;
    detail.fractal.amplitude = 3.0f;
    detail.warpAmplitude = 4.0f;
    params.layers.push_back(detail);
    return params;
}

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

TEST(ValidateSyntheticTerrain, ChecksLayerCountAndNamesTheBadLayer) {
    EXPECT_NO_THROW(ValidateSyntheticTerrain(SyntheticTerrainParams{}));

    SyntheticTerrainParams empty;
    empty.layers.clear();
    EXPECT_THROW(ValidateSyntheticTerrain(empty), std::invalid_argument);

    SyntheticTerrainParams tooMany;
    tooMany.layers.resize(kMaxNoiseLayers + 1);
    EXPECT_THROW(ValidateSyntheticTerrain(tooMany), std::invalid_argument);

    SyntheticTerrainParams bad = TwoLayers();
    bad.layers[1].fractal.gain = 0.0f;
    try {
        ValidateSyntheticTerrain(bad);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()).rfind("Layer 2:", 0), 0u) << error.what();
    }
}

TEST(FillSyntheticTerrain, WritesLayerHeightsAtCellCentres) {
    Raster<float> raster(8, 4);
    raster.SetTransform(GeoTransform::NorthUp(100.0, 50.0, 2.0));
    const SyntheticTerrainParams params = TwoLayers();
    FillSyntheticTerrain(raster, params);
    const auto centre = raster.CellCenter(3, 2);
    EXPECT_EQ(raster(3, 2),
              EvaluateNoiseLayers(static_cast<float>(centre.x), static_cast<float>(centre.y),
                                  params.layers.data(), params.layers.size()));
}

TEST(FillSyntheticTerrain, TilesMatchTheWholeRaster) {
    const SyntheticTerrainParams params = TwoLayers();
    constexpr std::size_t kSize = 64;
    constexpr std::size_t kTile = 16;
    constexpr double kPixel = 0.5;
    Raster<float> whole(kSize, kSize);
    whole.SetTransform(GeoTransform::NorthUp(-1000.0, 2000.0, kPixel));
    FillSyntheticTerrain(whole, params);

    for (std::size_t tileRow = 0; tileRow < kSize / kTile; ++tileRow) {
        for (std::size_t tileCol = 0; tileCol < kSize / kTile; ++tileCol) {
            Raster<float> tile(kTile, kTile);
            tile.SetTransform(GeoTransform::NorthUp(
                -1000.0 + static_cast<double>(tileCol * kTile) * kPixel,
                2000.0 - static_cast<double>(tileRow * kTile) * kPixel, kPixel));
            FillSyntheticTerrain(tile, params);
            for (std::size_t row = 0; row < kTile; ++row) {
                for (std::size_t col = 0; col < kTile; ++col) {
                    ASSERT_EQ(tile(col, row), whole(tileCol * kTile + col, tileRow * kTile + row));
                }
            }
        }
    }
}

TEST(FillSyntheticTerrain, RejectsInvalidParams) {
    Raster<float> raster(2, 2);
    SyntheticTerrainParams params;
    params.layers[0].fractal.octaves = 0;
    EXPECT_THROW(FillSyntheticTerrain(raster, params), std::invalid_argument);
}

TEST(GenerateSyntheticTerrain, HasRequestedSizeAndIsCentredOnTheOrigin) {
    const Raster<float> raster = GenerateSyntheticTerrain(8, 6, 2.0, SyntheticTerrainParams{});
    EXPECT_EQ(raster.Width(), 8u);
    EXPECT_EQ(raster.Height(), 6u);
    EXPECT_EQ(raster.Transform(), GeoTransform::NorthUp(-8.0, 6.0, 2.0));
    const auto first = raster.CellCenter(0, 0);
    const auto last = raster.CellCenter(7, 5);
    EXPECT_DOUBLE_EQ(first.x + last.x, 0.0);
    EXPECT_DOUBLE_EQ(first.y + last.y, 0.0);
}

TEST(GenerateSyntheticTerrain, MatchesFillOnTheSameGrid) {
    const SyntheticTerrainParams params = TwoLayers();
    const Raster<float> generated = GenerateSyntheticTerrain(17, 9, 1.5, params);
    Raster<float> filled(17, 9);
    filled.SetTransform(generated.Transform());
    FillSyntheticTerrain(filled, params);
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], filled.Values()[i]);
    }
}

TEST(GenerateSyntheticTerrain, RejectsInvalidArguments) {
    const SyntheticTerrainParams valid;
    EXPECT_THROW(GenerateSyntheticTerrain(0, 4, 1.0, valid), std::invalid_argument);
    EXPECT_THROW(GenerateSyntheticTerrain(4, 0, 1.0, valid), std::invalid_argument);
    EXPECT_THROW(GenerateSyntheticTerrain(4, 4, 0.0, valid), std::invalid_argument);
    EXPECT_THROW(GenerateSyntheticTerrain(4, 4, std::numeric_limits<double>::quiet_NaN(), valid),
                 std::invalid_argument);
    SyntheticTerrainParams invalid;
    invalid.layers.clear();
    EXPECT_THROW(GenerateSyntheticTerrain(4, 4, 1.0, invalid), std::invalid_argument);
}

TEST(SyntheticTerrainParams, ComparesLayers) {
    EXPECT_EQ(SyntheticTerrainParams{}, SyntheticTerrainParams{});
    EXPECT_FALSE(TwoLayers() == SyntheticTerrainParams{});
}

TEST(GenerateSyntheticTerrain, LogsTheFillWithItsTiming) {
    const terraingen::testing::LogCapture capture;
    SyntheticTerrainParams params;
    params.layers.push_back(params.layers.front());
    GenerateSyntheticTerrain(9, 7, 1.0, params);
    const auto records = capture.Matching("surface", terraingen::LogLevel::Info);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].message.rfind("Filled 9x7 synthetic terrain (2 layers) in ", 0), 0u)
        << records[0].message;
}
