/// A tile pyramid of the whole Moon, as a globe renderer streams it.
///
/// `TileScheme::WholeBody` is a longitude-latitude quadtree over a sphere; level L has 2^(L+1) x
/// 2^L tiles. A `TileCache` evaluates tiles on demand and keeps the most recently used up to a
/// byte budget. Writes levels 0 to 2 as GeoTIFFs in a geographic CRS for the Moon's sphere, one
/// folder per level.
///
///     example_tiles_body_pyramid [--output-dir DIR] [--test]
///     terraingen-examples tiles_body_pyramid

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

constexpr std::size_t kCells = 64;
constexpr int kLevels = 3;

class TilesBodyPyramid : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        moon_ = terraingen::BodySphere(terraingen::Body::Moon);
        terraingen::NoiseLayer broad;
        broad.fractal.seed = 8;
        broad.fractal.wavelength = 400000.0f;
        broad.fractal.amplitude = 3000.0f;
        broad.fractal.octaves = 6;
        const auto ages = std::make_shared<terraingen::UniformAges>(4.0);
        terraingen::CraterLayerOptions options;
        options.generate = false;
        options.surface = moon_;
        options.generation.manual.Append({0.0, 0.0, 300000.0, 3.9, 0u, true});
        options.generation.manual.Append({-60.0, 30.0, 200000.0, 3.5, 0u, true});

        terraingen::Terrain terrain;
        terrain.surface = moon_;
        terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0);
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{broad}},
                                                      terraingen::Vec2d{}, moon_),
            std::make_shared<terraingen::CraterLayer>(
                terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(1e-15, 2.0),
                                         ages,
                                         std::make_shared<terraingen::FreshnessDegradation>(
                                             ages, terraingen::LunarFreshCraterMorphometry),
                                         std::make_shared<terraingen::FassettThomsonProfile>()},
                options)};

        const terraingen::TileScheme scheme = terraingen::TileScheme::WholeBody(kCells);
        terraingen::TileCache cache(terrain, scheme, std::size_t{64} << 20);
        levels_.assign(kLevels, {});
        char line[160];
        for (int level = 0; level < kLevels; ++level) {
            const auto start = std::chrono::steady_clock::now();
            for (const terraingen::TileKey& key : terraingen::TileScheme::BodyTiles(level)) {
                levels_[static_cast<std::size_t>(level)].push_back({key, cache.Get(key)});
            }
            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            std::snprintf(line, sizeof(line), "level %d: %zu tiles at %.0f km spacing in %.2f s\n",
                          level, levels_[static_cast<std::size_t>(level)].size(),
                          scheme.Spacing(level) * moon_.UnitLength() / 1000.0, elapsed.count());
            Out() << line;
        }
        std::snprintf(line, sizeof(line), "%zu kB held, %zu evictions\n", cache.Size() >> 10,
                      cache.Evictions());
        Out() << line;

        mosaic_ = Mosaic(kLevels - 1, scheme);
        Show("level 2 mosaic (equirectangular)", mosaic_);
    }

    void Save(const std::filesystem::path& dir) override {
        const terraingen::GeoKeys keys = terraingen::GeographicSphereKeys(moon_.Radius());
        for (int level = 0; level < kLevels; ++level) {
            const std::filesystem::path folder =
                dir / "example_tiles_body_pyramid" / std::to_string(level);
            std::filesystem::create_directories(folder);
            for (const Tile& tile : levels_[static_cast<std::size_t>(level)]) {
                terraingen::WriteGeoTiff(folder / (std::to_string(tile.key.x) + "_" +
                                                   std::to_string(tile.key.y) + ".tif"),
                                         tile.terrain->heights, keys);
            }
        }
        Out() << "Wrote " << (dir / "example_tiles_body_pyramid").string() << "\n";
    }

    void Test() override {
        for (int level = 0; level < kLevels; ++level) {
            Check(levels_[static_cast<std::size_t>(level)].size() ==
                      (std::size_t{2} << level) * (std::size_t{1} << level),
                  "level " + std::to_string(level) + " has 2^(L+1) x 2^L tiles");
        }
        bool sized = true;
        bool finite = true;
        for (const auto& tiles : levels_) {
            for (const Tile& tile : tiles) {
                const auto& heights = tile.terrain->heights;
                sized = sized && heights.Width() == kCells && heights.Height() == kCells;
                for (std::size_t row = 0; row < heights.Height(); ++row) {
                    for (std::size_t col = 0; col < heights.Width(); ++col) {
                        finite = finite && std::isfinite(heights(col, row));
                    }
                }
            }
        }
        Check(sized, "every tile is 64 x 64");
        Check(finite, "every height is finite");

        float lowest = mosaic_(0, 0);
        float highest = lowest;
        for (std::size_t row = 0; row < mosaic_.Height(); ++row) {
            for (std::size_t col = 0; col < mosaic_.Width(); ++col) {
                lowest = std::min(lowest, mosaic_(col, row));
                highest = std::max(highest, mosaic_(col, row));
            }
        }
        Check(lowest > -20000.0f && highest < 20000.0f && highest - lowest > 1000.0f,
              "heights span kilometres, within 20 km of the datum");

        double inner = 0.0;
        double seams = 0.0;
        const std::size_t width = mosaic_.Width();
        for (std::size_t row = 0; row < mosaic_.Height(); ++row) {
            for (std::size_t col = 0; col < width; ++col) {
                const double step = std::fabs(mosaic_((col + 1) % width, row) - mosaic_(col, row));
                if ((col + 1) % kCells == 0) {
                    seams = std::max(seams, step);
                } else {
                    inner = std::max(inner, step);
                }
            }
        }
        Check(seams <= 1.5 * inner + 1.0,
              "the steps across level 2 tile seams, the antimeridian included, are no larger "
              "than inside the tiles");
    }

  private:
    struct Tile {
        terraingen::TileKey key;
        std::shared_ptr<const terraingen::EvaluatedTerrain> terrain;
    };

    /// The tiles of `level` side by side in an equirectangular projection, in metres at the
    /// equator, centred on longitude 0 and the equator.
    terraingen::Raster<float> Mosaic(int level, const terraingen::TileScheme& scheme) const {
        const auto& tiles = levels_[static_cast<std::size_t>(level)];
        const std::size_t columns = std::size_t{2} << level;
        const std::size_t rows = std::size_t{1} << level;
        terraingen::Raster<float> mosaic(columns * kCells, rows * kCells);
        for (const Tile& tile : tiles) {
            const auto& heights = tile.terrain->heights;
            for (std::size_t row = 0; row < kCells; ++row) {
                for (std::size_t col = 0; col < kCells; ++col) {
                    mosaic(static_cast<std::size_t>(tile.key.x) * kCells + col,
                           static_cast<std::size_t>(tile.key.y) * kCells + row) = heights(col, row);
                }
            }
        }
        const double metres = scheme.Spacing(level) * moon_.UnitLength();
        mosaic.SetTransform(terraingen::GeoTransform::NorthUp(
            -0.5 * static_cast<double>(mosaic.Width()) * metres,
            0.5 * static_cast<double>(mosaic.Height()) * metres, metres));
        return mosaic;
    }

    terraingen::Surface moon_;
    std::vector<std::vector<Tile>> levels_;
    terraingen::Raster<float> mosaic_;
};

}  // namespace

TERRAINGEN_EXAMPLE(TilesBodyPyramid,
                   "A tile pyramid of the whole Moon, as a globe renderer streams it")
