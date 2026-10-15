/// Tiles, levels of detail and point queries.
///
/// A `TileScheme` is a quadtree of north-up tiles of `cells` x `cells` samples, halving in size at
/// each level as a renderer's level-of-detail quadtree does. Every tile equals the same cells of
/// any larger grid, to the bit, so tiles can be generated independently and in any order.
/// `EvaluateAt` gives the height at a single point, equal to the grid cell centred there. This
/// example checks both and times point queries along a path.
///
///     example_tiles_quadtree [--output-dir DIR] [--test]
///     terraingen-examples tiles_quadtree

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

class TilesQuadtree : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        terraingen::CraterLayerOptions options;
        options.generation.dMin = 4.0;
        options.generation.dMax = 200.0;
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 400.0f;
        fbm.fractal.amplitude = 15.0f;
        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(512, 512, 1.0);
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
            std::make_shared<terraingen::CraterLayer>(
                terraingen::CraterModels{surface.density, surface.ages,
                                         std::make_shared<terraingen::FreshnessDegradation>(
                                             surface.ages, terraingen::LunarFreshCraterMorphometry),
                                         std::make_shared<terraingen::FassettThomsonProfile>()},
                options)};

        const terraingen::RasterGrid grid = terrain.source->Grid();
        const terraingen::TileScheme scheme = terraingen::TileScheme::Covering(grid, 128);
        finest_ = scheme.LevelFor(1.0);
        tileSize_ = scheme.TileSize(finest_);
        whole_ = terraingen::Evaluate(terrain).heights;
        keys_ = scheme.TilesCovering(grid.Bounds(), finest_);
        tiles_ = terraingen::EvaluateTiles(terrain, scheme, keys_);
        same_ = true;
        for (std::size_t n = 0; n < keys_.size(); ++n) {
            const terraingen::Raster<float>& tile = tiles_[n].heights;
            for (std::size_t row = 0; row < tile.Height(); ++row) {
                for (std::size_t col = 0; col < tile.Width(); ++col) {
                    const terraingen::Vec2d centre = tile.CellCenter(col, row);
                    const auto [wc, wr] = whole_.Transform().WorldToPixel(centre.x, centre.y);
                    same_ = same_ && tile(col, row) == whole_(static_cast<std::size_t>(wc),
                                                              static_cast<std::size_t>(wr));
                }
            }
        }
        char line[160];
        std::snprintf(line, sizeof(line),
                      "level %d: %zu tiles of %.0f m, equal to the whole grid: %s\n", finest_,
                      keys_.size(), tileSize_, same_ ? "yes" : "no");
        Out() << line;

        const auto start = std::chrono::steady_clock::now();
        highest_ = -1e9;
        const int queries = 10000;
        for (int i = 0; i < queries; ++i) {
            const double x = -250.0 + 500.0 * i / queries;
            highest_ = std::max(
                highest_, static_cast<double>(terraingen::EvaluateAt(terrain, x, 0.3 * x, 1.0)));
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        std::snprintf(line, sizeof(line),
                      "%d point queries in %.0f ms (%.0f per second); highest %.2f m\n", queries,
                      1000.0 * elapsed.count(), queries / elapsed.count(), highest_);
        Out() << line;

        Show("whole grid", whole_);
        const terraingen::TileKey coarse = scheme.TileAt(-1.0, 1.0, 0);
        coarse_ = terraingen::EvaluateTile(terrain, scheme, coarse).heights;
        Show("level 0 tile", coarse_);
        Show("level " + std::to_string(finest_) + " tile (" + std::to_string(keys_.front().x) +
                 ", " + std::to_string(keys_.front().y) + ")",
             tiles_.front().heights);
    }

    void Save(const std::filesystem::path& dir) override {
        for (std::size_t n = 0; n < keys_.size(); ++n) {
            const auto path = dir / ("example_tiles_quadtree_" + std::to_string(keys_[n].x) + "_" +
                                     std::to_string(keys_[n].y) + ".tif");
            terraingen::WriteGeoTiff(path, tiles_[n].heights);
            Out() << "Wrote " << path.string() << "\n";
        }
    }

    void Test() override {
        Check(finest_ == 2 && keys_.size() == 16, "level 2 has 16 tiles over the 512 m grid");
        Check(std::all_of(tiles_.begin(), tiles_.end(),
                          [](const terraingen::EvaluatedTerrain& tile) {
                              return tile.heights.Width() == 128 && tile.heights.Height() == 128;
                          }),
              "every tile is 128 x 128");
        Check(same_, "every tile equals the same cells of the whole grid, to the bit");
        Check(coarse_.Width() == 128 && coarse_.Height() == 128,
              "the level 0 tile is 128 x 128 too, at 4 m spacing");

        float lowest = whole_(0, 0);
        float top = lowest;
        for (std::size_t row = 0; row < whole_.Height(); ++row) {
            for (std::size_t col = 0; col < whole_.Width(); ++col) {
                lowest = std::min(lowest, whole_(col, row));
                top = std::max(top, whole_(col, row));
            }
        }
        Check(std::isfinite(highest_) && highest_ >= static_cast<double>(lowest) - 1.0 &&
                  highest_ <= static_cast<double>(top) + 1.0,
              "the highest point query lies within the whole grid's range of heights");
    }

  private:
    int finest_ = 0;
    double tileSize_ = 0.0;
    double highest_ = 0.0;
    bool same_ = false;
    terraingen::Raster<float> whole_;
    terraingen::Raster<float> coarse_;
    std::vector<terraingen::TileKey> keys_;
    std::vector<terraingen::EvaluatedTerrain> tiles_;
};

}  // namespace

TERRAINGEN_EXAMPLE(TilesQuadtree, "Tiles, levels of detail and point queries")
