// SPDX-License-Identifier: BSD-3-Clause

/// Albedo for a cratered synthetic terrain: a `SyntheticAlbedo` of 0.11 with gentle noise, and a
/// `CraterAlbedo` layer that reads the terrain's crater layer, brightening fresh craters (optical
/// maturity) and giving young ones rays. A hand-placed 2 Myr old crater shows the rays. The same
/// terrain as examples/config/albedo/example_albedo_crater_rays.json.
///
///     example_albedo_crater_rays [--output-dir DIR] [--test]
///     terraingen-examples albedo_crater_rays

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace {

class AlbedoCraterRays : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 3000.0f;
        fbm.fractal.amplitude = 60.0f;

        const auto production = std::make_shared<terraingen::SplicedSfd>(
            terraingen::ExtendedNeukumProductionFunction());
        const auto equilibrium =
            std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
        const auto density = std::make_shared<terraingen::MinimumSfd>(
            std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
                std::make_shared<terraingen::ProductionSurface>(production, 3.2,
                                                                terraingen::NeukumLunarChronology),
                equilibrium});
        const auto ages = std::make_shared<terraingen::SteadyStateAges>(
            production, equilibrium, 3.2, terraingen::NeukumLunarChronology);
        terraingen::CraterLayerOptions craterOptions;
        craterOptions.seed = 1;
        craterOptions.generation.dMin = 30.0;
        craterOptions.generation.dMax = 400.0;
        craterOptions.generation.manual.Append(terraingen::Crater{0.0, 0.0, 250.0, 0.002});
        const auto craters = std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{density, ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         ages, terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            craterOptions);

        terraingen::TerrainGenerator terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 8.0);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
        terrain.layers.push_back(craters);

        terraingen::NoiseLayer variation;
        variation.fractal.seed = 4;
        variation.fractal.wavelength = 1500.0f;
        variation.fractal.amplitude = 0.02f;
        variation.fractal.octaves = 4;
        terraingen::SyntheticAlbedoOptions base;
        base.value = 0.11;
        base.valueNoise.push_back(variation);

        terraingen::Albedo albedo;
        albedo.source = std::make_shared<terraingen::SyntheticAlbedo>(base);
        albedo.layers.push_back(std::make_shared<terraingen::CraterAlbedo>(craters));

        evaluated_ = terraingen::Evaluate(terrain);
        craters_ =
            terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        const terraingen::RasterGrid grid = terraingen::RasterGrid::FromRaster(evaluated_);
        bands_ = terraingen::EvaluateAlbedo(albedo, grid, {8.0, 0});
        stats_ = terraingen::ComputeStats(bands_.front());
        Out() << "Albedo from " << stats_.min << " to " << stats_.max << " over "
              << craters_.craters.Size() << " craters\n";
        Show("heights and albedo", evaluated_, &bands_.front());
    }

    void Save(const std::filesystem::path& dir) override {
        const auto heights = dir / "example_albedo_crater_rays_heights.tif";
        const auto path = dir / "example_albedo_crater_rays.tif";
        terraingen::WriteGeoTiff(heights, evaluated_);
        terraingen::WriteGeoTiff(path, bands_);
        Out() << "Wrote " << heights.string() << " and " << path.string() << "\n";
    }

    void Test() override {
        const terraingen::Raster<float>& band = bands_.front();
        Check(evaluated_.Width() == 513 && evaluated_.Height() == 513, "the heights are 513 x 513");
        Check(bands_.size() == 1 && band.Width() == 513 && band.Height() == 513,
              "one albedo band on the heights' grid");
        Check(stats_.validCount == band.Width() * band.Height(), "every albedo value is finite");
        Check(stats_.min > 0.08 && stats_.max < 0.2, "the albedo stays near 0.11");
        Check(stats_.max > stats_.mean + 0.005, "fresh craters brighten the albedo");
        Check(craters_.craters.Size() > 500, "more than 500 craters");
    }

  private:
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
    std::vector<terraingen::Raster<float>> bands_;
    terraingen::RasterStats stats_;
};

}  // namespace

TERRAINGEN_EXAMPLE(AlbedoCraterRays, "Albedo with bright young craters and rays")
