/// A stack of noise layers, each combined with the heights so far by its blend mode: rolling fBm,
/// domain-warped ridged crests added on top, and broad value noise as a mask that roughens some
/// regions and calms others. The gain of the crests is set from a Hurst exponent. The same terrain
/// as examples/config/terrain/example_terrain_layered.json, plus the warp.
///
///     example_terrain_layered [--output-dir DIR] [--test]
///     terraingen-examples terrain_layered

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace {

class TerrainLayered : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer rolling;
        rolling.fractal.seed = 1;
        rolling.fractal.wavelength = 800.0f;
        rolling.fractal.amplitude = 40.0f;

        terraingen::NoiseLayer crests;
        crests.fractal.seed = 2;
        crests.fractal.type = terraingen::FractalType::Ridged;
        crests.fractal.wavelength = 250.0f;
        crests.fractal.amplitude = 8.0f;
        crests.fractal.octaves = 6;
        crests.fractal.gain = terraingen::GainFromHurst(0.7f, crests.fractal.lacunarity);
        crests.blend = terraingen::LayerBlend::Add;
        crests.warpAmplitude = 60.0f;
        crests.warpWavelength = 400.0f;

        terraingen::NoiseLayer mask;
        mask.fractal.seed = 3;
        mask.fractal.basis = terraingen::NoiseBasis::Value;
        mask.fractal.wavelength = 1200.0f;
        mask.fractal.amplitude = 1.0f;
        mask.fractal.octaves = 2;
        mask.blend = terraingen::LayerBlend::Mask;

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
        terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(
            terraingen::SyntheticTerrainParams{{rolling, crests, mask}}));

        terraingen::Terrain rollingOnly;
        rollingOnly.source = terrain.source;
        rollingOnly.layers.push_back(std::make_shared<terraingen::NoiseRelief>(
            terraingen::SyntheticTerrainParams{{rolling}}));

        evaluated_ = terraingen::Evaluate(terrain);
        rolling_ = terraingen::Evaluate(rollingOnly);
        stats_ = terraingen::ComputeStats(evaluated_);
        Out() << "Heights from " << stats_.min << " to " << stats_.max << " m\n";
        Show("layered", evaluated_);
        Show("rolling fBm only", rolling_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_terrain_layered.tif";
        terraingen::WriteGeoTiff(path, evaluated_);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        Check(evaluated_.Width() == 513 && evaluated_.Height() == 513, "the heights are 513 x 513");
        Check(stats_.validCount == evaluated_.Size(), "every height is finite");
        Check(stats_.max - stats_.min > 10.0 && stats_.max - stats_.min < 500.0,
              "the relief is tens of metres");
        double differs = 0.0;
        for (std::size_t i = 0; i < rolling_.Size(); ++i) {
            const float change = evaluated_.Values()[i] - rolling_.Values()[i];
            differs = std::max(differs, static_cast<double>(std::abs(change)));
        }
        Check(differs > 0.5, "the crests and mask change the rolling fBm");
    }

  private:
    terraingen::Raster<float> evaluated_;
    terraingen::Raster<float> rolling_;
    terraingen::RasterStats stats_;
};

}  // namespace

TERRAINGEN_EXAMPLE(TerrainLayered,
                   "Noise layers stacked by blend mode: fBm, warped ridges and a mask")
