/// Lunar craters on a synthetic terrain: a `CraterLayer` with the DSNE lunar surface model for a
/// 3.5 Gyr surface (crater density and ages), freshness degradation and the Fassett and Thomson
/// profile, added to the fBm terrain of example_terrain_fbm. The same terrain as
/// examples/config/craters/example_craters_simple.json.
///
///     example_craters_simple [--output-dir DIR] [--test]
///     terraingen-examples craters_simple

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace {

class CratersSimple : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 800.0f;
        fbm.fractal.amplitude = 40.0f;

        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        const terraingen::CraterModels models{
            surface.density, surface.ages,
            std::make_shared<terraingen::FreshnessDegradation>(
                surface.ages, terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
        terraingen::CraterLayerOptions options;
        options.seed = 1;
        options.generation.dMin = 8.0;
        options.generation.dMax = 300.0;

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
        terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));

        evaluated_ = terraingen::Evaluate(terrain);
        const auto& diameters = evaluated_.features.craters.Diameters();
        Out() << evaluated_.features.craters.Size() << " craters, the largest "
              << *std::max_element(diameters.begin(), diameters.end()) << " m across\n";
        for (const double d : {8.0, 16.0, 32.0, 64.0, 128.0}) {
            Out() << "  N(>= " << d << " m) = "
                  << std::count_if(diameters.begin(), diameters.end(),
                                   [d](double diameter) { return diameter >= d; })
                  << "\n";
        }
        Show("heights", evaluated_.heights);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto heights = dir / "example_craters_simple.tif";
        const auto list = dir / "example_craters_simple.csv";
        terraingen::WriteGeoTiff(heights, evaluated_.heights);
        terraingen::WriteCraterList(evaluated_.features.craters, list, terraingen::ListFormat::Csv);
        Out() << "Wrote " << heights.string() << " and " << list.string() << "\n";
    }

    void Test() override {
        const auto& diameters = evaluated_.features.craters.Diameters();
        Check(evaluated_.heights.Width() == 513 && evaluated_.heights.Height() == 513,
              "the heights are 513 x 513");
        Check(diameters.size() > 100, "more than 100 craters");
        Check(std::all_of(diameters.begin(), diameters.end(),
                          [](double d) { return d >= 8.0 && d <= 300.0; }),
              "every crater is 8 to 300 m across");
        const auto small =
            std::count_if(diameters.begin(), diameters.end(), [](double d) { return d < 16.0; });
        Check(2 * small > static_cast<std::ptrdiff_t>(diameters.size()),
              "most craters are under 16 m, as a steep size-frequency distribution makes them");
    }

  private:
    terraingen::EvaluatedTerrain evaluated_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersSimple, "Lunar craters on a synthetic terrain")
