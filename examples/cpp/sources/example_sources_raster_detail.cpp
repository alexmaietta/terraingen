/// Detail on a coarse DEM: a `GeoTiffSource` resampled to a finer pixel, plus fine noise and
/// craters smaller than the DEM's cells. The example first writes a coarse 16 m DEM of its own to
/// stand in for orbital data, then builds the 2 m terrain on it. A `SourceChain` fills any gaps in
/// the DEM from a flat datum.
///
///     example_sources_raster_detail [--output-dir DIR] [--test]
///     terraingen-examples sources_raster_detail

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

namespace {

terraingen::CraterModels LunarModels() {
    const auto production =
        std::make_shared<terraingen::SplicedSfd>(terraingen::ExtendedNeukumProductionFunction());
    const auto equilibrium =
        std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
    const auto density = std::make_shared<terraingen::MinimumSfd>(
        std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
            std::make_shared<terraingen::ProductionSurface>(production, 3.5,
                                                            terraingen::NeukumLunarChronology),
            equilibrium});
    const auto ages = std::make_shared<terraingen::SteadyStateAges>(
        production, equilibrium, 3.5, terraingen::NeukumLunarChronology);
    return {density, ages,
            std::make_shared<terraingen::FreshnessDegradation>(
                ages, terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
}

class SourcesRasterDetail : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer broad;
        broad.fractal.seed = 4;
        broad.fractal.wavelength = 800.0f;
        broad.fractal.amplitude = 40.0f;
        terraingen::CraterLayerOptions large;
        large.seed = 4;
        large.generation.dMin = 100.0;
        large.generation.dMax = 600.0;
        terraingen::Terrain coarse;
        coarse.source = std::make_shared<terraingen::FlatSource>(65, 65, 16.0);
        coarse.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{broad}}),
            std::make_shared<terraingen::CraterLayer>(LunarModels(), large)};
        coarse_ = terraingen::Evaluate(coarse);

        const std::filesystem::path temp =
            std::filesystem::temp_directory_path() /
            ("terraingen_example_sources_raster_detail_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(temp);
        const std::filesystem::path dem = temp / "example_sources_raster_detail_coarse.tif";
        terraingen::WriteGeoTiff(dem, coarse_);

        const auto raster = std::make_shared<terraingen::GeoTiffSource>(
            dem, terraingen::GeoTiffSourceOptions{.pixelSize = 2.0});
        const auto chain = std::make_shared<terraingen::SourceChain>(
            std::vector<std::shared_ptr<const terraingen::Source>>{
                raster, std::make_shared<terraingen::FlatSource>(1, 1, 2.0)});
        const terraingen::RasterGrid grid = raster->Grid();
        Out() << "16 m DEM resampled to " << grid.width << " x " << grid.height
              << " cells of 2 m\n";

        terraingen::NoiseLayer detail;
        detail.fractal.seed = 6;
        detail.fractal.wavelength = 60.0f;
        detail.fractal.amplitude = 1.5f;
        detail.fractal.octaves = 5;
        terraingen::CraterLayerOptions small;
        small.seed = 5;
        small.generation.dMin = 6.0;
        small.generation.dMax = 100.0;
        terraingen::Terrain terrain;
        terrain.source = chain;
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{detail}}),
            std::make_shared<terraingen::CraterLayer>(LunarModels(), small)};
        evaluated_ = terraingen::Evaluate(terrain);
        craters_ = terraingen::PlacedCraters(terrain, terraingen::RasterGrid::Of(evaluated_));
        std::filesystem::remove_all(temp);
        Out() << "added " << craters_.craters.Size() << " craters of 6 to 100 m\n";
        Show("heights", evaluated_);
        Show("coarse DEM", coarse_);
    }

    void Save(const std::filesystem::path& dir) override {
        terraingen::WriteGeoTiff(dir / "example_sources_raster_detail_coarse.tif", coarse_);
        terraingen::WriteGeoTiff(dir / "example_sources_raster_detail.tif", evaluated_);
    }

    void Test() override {
        const auto& heights = evaluated_;
        Check(coarse_.Width() == 65 && coarse_.Height() == 65, "the coarse DEM is 65 x 65");
        Check(heights.Width() >= 512 && heights.Width() <= 528 && heights.Height() >= 512 &&
                  heights.Height() <= 528,
              "the 16 m DEM resamples to about 520 x 520 cells of 2 m");
        Check(std::abs(heights.Transform().pixelWidth - 2.0) < 1e-9, "the cells are 2 m");
        Check(std::all_of(heights.Values().begin(), heights.Values().end(),
                          [](float h) { return std::isfinite(h); }),
              "the heights are finite");
        const auto& diameters = craters_.craters.Diameters();
        Check(diameters.size() > 500 &&
                  std::all_of(diameters.begin(), diameters.end(),
                              [](double d) { return d >= 6.0 && d <= 100.0; }),
              "more than 500 detail craters, each 6 to 100 m across");
    }

  private:
    terraingen::Raster<float> coarse_;
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
};

}  // namespace

TERRAINGEN_EXAMPLE(SourcesRasterDetail, "Detail on a coarse DEM")
