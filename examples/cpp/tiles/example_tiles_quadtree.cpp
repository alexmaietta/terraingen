/// Tiles, levels of detail and point queries.
///
/// A `TileScheme` is a quadtree of north-up tiles of `cells` x `cells` samples, halving in size at
/// each level as a renderer's level-of-detail quadtree does. Every tile equals the same cells of
/// any larger grid, to the bit, so tiles can be generated independently and in any order.
/// `EvaluateAt` gives the height at a single point, equal to the grid cell centred there. This
/// example checks both and times point queries along a path.
///
///     example_tiles_quadtree [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

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
    const int finest = scheme.LevelFor(1.0);
    const terraingen::Raster<float> whole = terraingen::Evaluate(terrain).heights;
    const auto keys = scheme.TilesCovering(grid.Bounds(), finest);
    const auto tiles = terraingen::EvaluateTiles(terrain, scheme, keys);
    bool same = true;
    for (std::size_t n = 0; n < keys.size(); ++n) {
        const terraingen::Raster<float>& tile = tiles[n].heights;
        for (std::size_t row = 0; row < tile.Height(); ++row) {
            for (std::size_t col = 0; col < tile.Width(); ++col) {
                const terraingen::Vec2d centre = tile.CellCenter(col, row);
                const auto [wc, wr] = whole.Transform().WorldToPixel(centre.x, centre.y);
                same = same && tile(col, row) == whole(static_cast<std::size_t>(wc),
                                                       static_cast<std::size_t>(wr));
            }
        }
        terraingen::WriteGeoTiff(output / ("example_tiles_quadtree_" + std::to_string(keys[n].x) +
                                           "_" + std::to_string(keys[n].y) + ".tif"),
                                 tile);
    }
    std::printf("level %d: %zu tiles of %.0f m, equal to the whole grid: %s\n", finest, keys.size(),
                scheme.TileSize(finest), same ? "yes" : "no");

    const auto start = std::chrono::steady_clock::now();
    double highest = -1e9;
    const int queries = 10000;
    for (int i = 0; i < queries; ++i) {
        const double x = -250.0 + 500.0 * i / queries;
        highest = std::max(highest,
                           static_cast<double>(terraingen::EvaluateAt(terrain, x, 0.3 * x, 1.0)));
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::printf("%d point queries in %.0f ms (%.0f per second); highest %.2f m\n", queries,
                1000.0 * elapsed.count(), queries / elapsed.count(), highest);
    return 0;
}
