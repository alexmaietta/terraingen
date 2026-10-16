/// Regions: different layers in different parts of the terrain. A `RegionLayer` applies a layer
/// only inside a rectangle or circle, faded out over `feather` metres outside it. A circular mare
/// is flattened and lowered, the west half is an old, heavily cratered surface and the east half a
/// younger one. Writes the heights.
///
///     example_layers_regions [--output-dir DIR] [--test]
///     terraingen-examples layers_regions

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>

namespace {

std::shared_ptr<terraingen::CraterLayer> Craters(double ageGyr, std::uint32_t seed) {
    const auto production =
        std::make_shared<terraingen::SplicedSfd>(terraingen::ExtendedNeukumProductionFunction());
    const auto equilibrium =
        std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
    const auto density = std::make_shared<terraingen::MinimumSfd>(
        std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
            std::make_shared<terraingen::ProductionSurface>(production, ageGyr,
                                                            terraingen::NeukumLunarChronology),
            equilibrium});
    const auto ages = std::make_shared<terraingen::SteadyStateAges>(
        production, equilibrium, ageGyr, terraingen::NeukumLunarChronology);
    terraingen::CraterLayerOptions options;
    options.seed = seed;
    options.generation.dMin = 10.0;
    options.generation.dMax = 200.0;
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{density, ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

class LayersRegions : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        constexpr double kHalf = 513 * 4.0 / 2;
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 9;
        fbm.fractal.wavelength = 1200.0f;
        fbm.fractal.amplitude = 25.0f;
        fbm.fractal.octaves = 6;
        mare_ = std::make_shared<terraingen::RegionLayer>(
            std::make_shared<terraingen::ScaleLayer>(0.2, -15.0),
            terraingen::RegionShape::Circle({0.0, 0.0}, 600.0), 150.0);

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 4.0);
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
            mare_,
            std::make_shared<terraingen::RegionLayer>(
                Craters(3.9, 9), terraingen::RegionShape::Rectangle({-kHalf, -kHalf, 0.0, kHalf}),
                200.0),
            std::make_shared<terraingen::RegionLayer>(
                Craters(1.0, 10), terraingen::RegionShape::Rectangle({0.0, -kHalf, kHalf, kHalf}),
                200.0)};
        evaluated_ = terraingen::Evaluate(terrain);
        craters_ = terraingen::PlacedCraters(terrain, terraingen::RasterGrid::Of(evaluated_));
        Out() << craters_.craters.Size() << " craters; mare weight at the centre " << std::fixed
              << std::setprecision(2) << mare_->Weight(0.0, 0.0) << ", 700 m out "
              << mare_->Weight(700.0, 0.0) << "\n"
              << std::defaultfloat;
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        terraingen::WriteGeoTiff(dir / "example_layers_regions.tif", evaluated_);
    }

    void Test() override {
        const auto& heights = evaluated_;
        Check(heights.Width() == 513 && heights.Height() == 513, "the heights are 513 x 513");
        Check(std::all_of(heights.Values().begin(), heights.Values().end(),
                          [](float h) { return std::isfinite(h); }),
              "the heights are finite");
        Check(craters_.craters.Size() > 1000, "more than 1000 craters");
        Check(mare_->Weight(0.0, 0.0) > 0.99 && mare_->Weight(700.0, 0.0) > 0.0 &&
                  mare_->Weight(700.0, 0.0) < 0.5 && mare_->Weight(800.0, 0.0) < 1e-6,
              "the mare weighs 1 inside its circle and fades out over the 150 m feather");
        const auto& xs = craters_.craters.X();
        const auto& ages = craters_.craters.AgesGyr();
        double oldestWest = 0.0;
        double oldestEast = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            if (xs[i] < -200.0) {
                oldestWest = std::max(oldestWest, ages[i]);
            } else if (xs[i] > 200.0) {
                oldestEast = std::max(oldestEast, ages[i]);
            }
        }
        Check(oldestEast > 0.0 && oldestEast <= 1.0 + 1e-6,
              "craters east of the feather are no older than the 1 Gyr east half");
        Check(oldestWest > 2.0, "the 3.9 Gyr west half has craters older than 2 Gyr");
    }

  private:
    std::shared_ptr<terraingen::RegionLayer> mare_;
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
};

}  // namespace

TERRAINGEN_EXAMPLE(LayersRegions, "Different layers in different parts of the terrain")
