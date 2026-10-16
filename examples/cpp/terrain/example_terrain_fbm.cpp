/// A synthetic terrain from library objects: a flat source with one fBm noise layer, composed into
/// a `Terrain`. Evaluates it over the source's grid, queries single points as a physics engine
/// would, and writes a GeoTIFF. The same terrain as
/// examples/config/terrain/example_terrain_fbm.json.
///
///     example_terrain_fbm [--output-dir DIR] [--test]
///     terraingen-examples terrain_fbm

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace {

class TerrainFbm : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 800.0f;
        fbm.fractal.amplitude = 40.0f;
        fbm.fractal.octaves = 8;

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));

        evaluated_ = terraingen::Evaluate(terrain);
        stats_ = terraingen::ComputeStats(evaluated_);
        Out() << "Heights from " << stats_.min << " to " << stats_.max << " m\n";

        points_.clear();
        for (const auto& [x, y] : {std::pair{0.0, 0.0}, std::pair{120.5, -310.25}}) {
            points_.push_back(terraingen::EvaluateAt(terrain, x, y, 2.0));
            Out() << "Height at (" << x << ", " << y << "): " << points_.back() << " m\n";
        }
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_terrain_fbm.tif";
        terraingen::WriteGeoTiff(path, evaluated_);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        Check(evaluated_.Width() == 513 && evaluated_.Height() == 513, "the heights are 513 x 513");
        Check(stats_.validCount == evaluated_.Size(), "every height is finite");
        Check(stats_.max - stats_.min > 10.0 && stats_.max - stats_.min < 400.0,
              "the relief is tens of metres, as a 40 m amplitude gives");
        bool pointsInRange = points_.size() == 2;
        for (const double h : points_) {
            pointsInRange =
                pointsInRange && std::isfinite(h) && h >= stats_.min - 1.0 && h <= stats_.max + 1.0;
        }
        Check(pointsInRange, "the point queries lie within the grid's height range");
    }

  private:
    terraingen::Raster<float> evaluated_;
    terraingen::RasterStats stats_;
    std::vector<double> points_;
};

}  // namespace

TERRAINGEN_EXAMPLE(TerrainFbm, "A synthetic fBm terrain from library objects")
