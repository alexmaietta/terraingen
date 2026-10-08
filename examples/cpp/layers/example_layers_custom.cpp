/// Sources, layers and filters of your own: every part of a terrain is an interface to derive
/// from. Here an inclined plane (`Source`), a sinuous rille (`Layer`) and a terrace quantization
/// (`Filter`) mix with built-in noise and craters. Writes the terrain with and without the filter.
///
///     example_layers_custom [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

/// A plane rising `slope` metres per metre towards the north-east over a 257 x 257 grid of 2 m.
class InclinedPlane final : public terraingen::Source {
  public:
    explicit InclinedPlane(double slope) : slope_(slope) {}

    terraingen::RasterGrid Grid() const override {
        return {257, 257, terraingen::GeoTransform::NorthUp(-257.0, 257.0, 2.0)};
    }

    void Fill(terraingen::Raster<float>& heights) const override {
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const terraingen::Vec2d centre = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(centre.x, centre.y);
            }
        }
    }

    float HeightAt(double x, double y) const override {
        return static_cast<float>(slope_ * (x + y) / std::sqrt(2.0));
    }

  private:
    double slope_;
};

/// A sinuous channel 12 m deep and 40 m wide, meandering north to south.
class Rille final : public terraingen::Layer {
  public:
    void Apply(terraingen::Raster<float>& heights, const terraingen::LayerContext& context,
               terraingen::TerrainFeatures& /*features*/) const override {
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const terraingen::Vec2d centre = heights.CellCenter(col, row);
                heights(col, row) = ApplyAt(heights(col, row), centre.x, centre.y, context);
            }
        }
    }

    float ApplyAt(float height, double x, double y,
                  const terraingen::LayerContext& /*context*/) const override {
        const double centre = 60.0 * std::sin(y / 70.0);
        const double offset = (x - centre) / 20.0;
        return height - static_cast<float>(12.0 * std::exp(-offset * offset));
    }

    double MaxFeatureDiameter() const override {
        return 40.0;
    }
};

/// Rounds heights halfway to steps of `step` metres.
class Terraces final : public terraingen::Filter {
  public:
    explicit Terraces(double step) : step_(step) {}

    void Apply(terraingen::Raster<float>& heights, double /*spacing*/,
               const terraingen::TerrainFeatures& /*features*/) const override {
        for (float& value : heights.Values()) {
            const double height = static_cast<double>(value);
            const double rounded = std::round(height / step_) * step_;
            value = static_cast<float>(0.5 * height + 0.5 * rounded);
        }
    }

    double Margin(double /*maxFeatureDiameter*/, double /*spacing*/) const override {
        return 0.0;
    }

  private:
    double step_;
};

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 1;
    fbm.fractal.wavelength = 200.0f;
    fbm.fractal.amplitude = 4.0f;
    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    terraingen::CraterLayerOptions options;
    options.generation.dMin = 8.0;
    options.generation.dMax = 80.0;

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<InclinedPlane>(0.02);
    terrain.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
        std::make_shared<Rille>(),
        std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{surface.density, surface.ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         surface.ages, terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            options)};
    std::printf("height at the origin: %.2f m\n",
                static_cast<double>(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0)));
    terraingen::WriteGeoTiff(output / "example_layers_custom.tif",
                             terraingen::Evaluate(terrain).heights);
    terrain.filters.push_back(std::make_shared<Terraces>(4.0));
    terraingen::WriteGeoTiff(output / "example_layers_custom_terraced.tif",
                             terraingen::Evaluate(terrain).heights);
    std::printf("Wrote example_layers_custom.tif and example_layers_custom_terraced.tif\n");
    return 0;
}
