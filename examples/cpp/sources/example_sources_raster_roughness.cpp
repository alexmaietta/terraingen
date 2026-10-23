/// Roughness continuing a coarse DEM below its cells: the example writes an 8 m DEM of its own to
/// stand in for orbital data, reads it upsampled to 0.5 m, estimates the Hurst exponent and RMS
/// slope its variogram extrapolates to a 1 m baseline (`EstimateSourceRoughness`), and adds a
/// `RoughnessLayer` of those values with octaves from the DEM's cell size down to the new one. A
/// configuration does the same with "estimate_from_base": true on its roughness layer.
///
///     example_sources_raster_roughness [--output-dir DIR] [--test]
///     terraingen-examples sources_raster_roughness

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

namespace {

class SourcesRasterRoughness : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer relief;
        relief.fractal.seed = 8;
        relief.fractal.wavelength = 200.0f;
        relief.fractal.amplitude = 12.0f;
        relief.fractal.octaves = 6;
        relief.fractal.gain = terraingen::GainFromHurst(0.8f, 2.0f);
        terraingen::TerrainGenerator coarse;
        coarse.source = std::make_shared<terraingen::FlatSource>(33, 33, 8.0);
        coarse.layers = {std::make_shared<terraingen::NoiseRelief>(
            terraingen::SyntheticTerrainParams{{relief}})};
        coarse_ = terraingen::Evaluate(coarse);

        const std::filesystem::path temp = std::filesystem::temp_directory_path() /
                                           ("terraingen_example_sources_raster_roughness_" +
                                            std::to_string(std::random_device{}()));
        std::filesystem::create_directories(temp);
        const std::filesystem::path dem = temp / "example_sources_raster_roughness_coarse.tif";
        terraingen::WriteGeoTiff(dem, coarse_);
        const auto raster = std::make_shared<terraingen::GeoTiffSource>(dem);
        const terraingen::RasterGrid grid = terraingen::ResampledGrid(raster->Grid(), 0.5);

        base_ = terraingen::EstimateSourceRoughness(*raster);
        terraingen::RoughnessOptions roughness;
        roughness.hurst = base_.hurst;
        roughness.baseline = 1.0;
        roughness.rmsSlope = base_.RmsSlope(roughness.baseline);
        roughness.maxWavelength = 8.0;
        roughness.minWavelength = 0.5;
        options_ = roughness;
        Out() << "the 8 m DEM has Hurst exponent " << base_.hurst << " and RMS slope "
              << base_.RmsSlope(8.0) << " over 8 m, " << roughness.rmsSlope
              << " extrapolated to 1 m\n";

        terraingen::TerrainGenerator terrain;
        terrain.source = raster;
        terrain.layers = {std::make_shared<terraingen::RoughnessLayer>(roughness)};
        terraingen::TerrainGenerator plain;
        plain.source = raster;
        smooth_ = terraingen::Evaluate(plain, grid);
        evaluated_ = terraingen::Evaluate(terrain, grid);
        std::filesystem::remove_all(temp);
        Show("heights", evaluated_);
        Show("resampled DEM", smooth_);
    }

    void Save(const std::filesystem::path& dir) override {
        terraingen::WriteGeoTiff(dir / "example_sources_raster_roughness_coarse.tif", coarse_);
        terraingen::WriteGeoTiff(dir / "example_sources_raster_roughness.tif", evaluated_);
    }

    void Test() override {
        Check(base_.cellSize == 8.0, "the estimate reads the DEM on its native 8 m cells");
        Check(base_.hurst > 0.5 && base_.hurst <= 1.0, "the DEM's Hurst exponent is in (0.5, 1]");
        Check(options_.rmsSlope > 0.0 && options_.rmsSlope < 1.0,
              "the extrapolated RMS slope is positive and below 1");
        Check(evaluated_.Width() == smooth_.Width() && evaluated_.Height() == smooth_.Height(),
              "the roughness keeps the DEM's 0.5 m grid");
        double sum = 0.0;
        for (std::size_t i = 0; i < evaluated_.Size(); ++i) {
            const double added = static_cast<double>(evaluated_.Values()[i]) -
                                 static_cast<double>(smooth_.Values()[i]);
            sum += added * added;
        }
        const double rms = std::sqrt(sum / static_cast<double>(evaluated_.Size()));
        Check(rms > 0.01 && rms < 2.0, "the roughness adds centimeters to decimeters of relief");
    }

  private:
    terraingen::Raster<float> coarse_;
    terraingen::Raster<float> smooth_;
    terraingen::Raster<float> evaluated_;
    terraingen::BaseRoughness base_;
    terraingen::RoughnessOptions options_;
};

}  // namespace

TERRAINGEN_EXAMPLE(SourcesRasterRoughness, "Roughness estimated from a coarse DEM")
