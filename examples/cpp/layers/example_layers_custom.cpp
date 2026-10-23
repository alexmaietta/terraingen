// SPDX-License-Identifier: BSD-3-Clause

/// Sources and layers of your own: every part of a terrain is an interface to derive from. Here
/// an inclined plane (`Source`), a sinuous rille and a terrace quantization (`FormulaLayer`s) mix
/// with built-in noise and craters. Writes the terrain with and without the terraces.
///
///     example_layers_custom [--output-dir DIR] [--test]
///     terraingen-examples layers_custom

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>

namespace {

/// A plane rising `slope` meters per meter toward the north-east over a 257 x 257 grid of 2 m.
class InclinedPlane final : public terraingen::Source {
  public:
    explicit InclinedPlane(double slope) : slope_(slope) {}

    terraingen::RasterGrid Grid() const override {
        return {257, 257, terraingen::GeoTransform::NorthUp(-257.0, 257.0, 2.0)};
    }

    void Fill(terraingen::Raster<float>& heights, double spacing) const override {
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const terraingen::Vec2d center = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(center.x, center.y, spacing);
            }
        }
    }

    float HeightAt(double x, double y, double /*spacing*/) const override {
        return static_cast<float>(slope_ * (x + y) / std::sqrt(2.0));
    }

  private:
    double slope_;
};

/// A sinuous channel 12 m deep and 40 m wide, meandering north to south.
class Rille final : public terraingen::FormulaLayer {
  protected:
    float HeightAt(float height, double x, double y, double /*spacing*/,
                   const terraingen::Ground* /*ground*/) const override {
        const double center = 60.0 * std::sin(y / 70.0);
        const double offset = (x - center) / 20.0;
        return height - static_cast<float>(12.0 * std::exp(-offset * offset));
    }
};

/// Rounds heights halfway to steps of `step` meters.
class Terraces final : public terraingen::FormulaLayer {
  public:
    explicit Terraces(double step) : step_(step) {}

  protected:
    float HeightAt(float height, double /*x*/, double /*y*/, double /*spacing*/,
                   const terraingen::Ground* /*ground*/) const override {
        const auto value = static_cast<double>(height);
        const double rounded = std::round(value / step_) * step_;
        return static_cast<float>(0.5 * value + 0.5 * rounded);
    }

  private:
    double step_;
};

/// The mean height of the `rows` x `cols` block of `heights` from (`col0`, `row0`).
double BlockMean(const terraingen::Raster<float>& heights, std::size_t col0, std::size_t row0,
                 std::size_t cols, std::size_t rows) {
    double sum = 0.0;
    for (std::size_t row = row0; row < row0 + rows; ++row) {
        for (std::size_t col = col0; col < col0 + cols; ++col) {
            sum += static_cast<double>(heights(col, row));
        }
    }
    return sum / static_cast<double>(cols * rows);
}

class LayersCustom : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 200.0f;
        fbm.fractal.amplitude = 4.0f;
        const auto production = std::make_shared<terraingen::SplicedSfd>(
            terraingen::ExtendedNeukumProductionFunction());
        const auto equilibrium =
            std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
        const auto density = std::make_shared<terraingen::MinimumSfd>(
            std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
                std::make_shared<terraingen::ProductionSurface>(production, 3.5,
                                                                terraingen::NeukumLunarChronology),
                equilibrium});
        const auto ages = std::make_shared<terraingen::SteadyStateAges>(
            production, equilibrium, 3.5, terraingen::NeukumLunarChronology);
        terraingen::CraterLayerOptions options;
        options.generation.dMin = 8.0;
        options.generation.dMax = 80.0;

        terraingen::TerrainGenerator terrain;
        terrain.source = std::make_shared<InclinedPlane>(0.02);
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
            std::make_shared<Rille>(),
            std::make_shared<terraingen::CraterLayer>(
                terraingen::CraterModels{density, ages,
                                         std::make_shared<terraingen::FreshnessDegradation>(
                                             ages, terraingen::LunarFreshCraterMorphometry),
                                         std::make_shared<terraingen::FassettThomsonProfile>()},
                options)};
        originHeight_ = terraingen::HeightAt(terrain, 0.0, 0.0, 2.0);
        Out() << "height at the origin: " << std::fixed << std::setprecision(2) << originHeight_
              << " m\n"
              << std::defaultfloat;
        plain_ = terraingen::Evaluate(terrain);
        terrain.layers.push_back(std::make_shared<Terraces>(4.0));
        terraced_ = terraingen::Evaluate(terrain);
        Show("heights", plain_);
        Show("terraced", terraced_);
    }

    void Save(const std::filesystem::path& dir) override {
        terraingen::WriteGeoTiff(dir / "example_layers_custom.tif", plain_);
        terraingen::WriteGeoTiff(dir / "example_layers_custom_terraced.tif", terraced_);
        Out() << "Wrote example_layers_custom.tif and example_layers_custom_terraced.tif\n";
    }

    void Test() override {
        Check(plain_.Width() == 257 && plain_.Height() == 257 && terraced_.Width() == 257 &&
                  terraced_.Height() == 257,
              "both terrains are 257 x 257");
        Check(std::isfinite(originHeight_) &&
                  std::all_of(plain_.Values().begin(), plain_.Values().end(),
                              [](float h) { return std::isfinite(h); }),
              "the heights are finite");
        Check(BlockMean(plain_, 192, 0, 64, 64) > BlockMean(plain_, 0, 192, 64, 64) + 5.0,
              "the inclined plane rises from the south-west to the north-east corner");
        double largestShift = 0.0;
        double totalShift = 0.0;
        for (std::size_t i = 0; i < plain_.Values().size(); ++i) {
            const double shift =
                static_cast<double>(std::abs(terraced_.Values()[i] - plain_.Values()[i]));
            largestShift = std::max(largestShift, shift);
            totalShift += shift;
        }
        Check(largestShift <= 1.0 + 1e-3,
              "the terraces move no height more than a quarter of their 4 m step");
        Check(totalShift > 0.0, "the terrace filter changes the heights");
    }

  private:
    float originHeight_ = 0.0f;
    terraingen::Raster<float> plain_;
    terraingen::Raster<float> terraced_;
};

}  // namespace

TERRAINGEN_EXAMPLE(LayersCustom, "Sources, layers and filters of your own")
