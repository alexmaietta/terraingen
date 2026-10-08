/// A tile pyramid of the whole Moon, as a globe renderer streams it.
///
/// `TileScheme::WholeBody` is a longitude-latitude quadtree over a sphere; level L has 2^(L+1) x
/// 2^L tiles. A `TileCache` evaluates tiles on demand and keeps the most recently used up to a
/// byte budget. Writes levels 0 to 2 as GeoTIFFs in a geographic CRS for the Moon's sphere, one
/// folder per level.
///
///     example_tiles_body_pyramid [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";

    const terraingen::Surface moon = terraingen::BodySphere(terraingen::Body::Moon);
    terraingen::NoiseLayer broad;
    broad.fractal.seed = 8;
    broad.fractal.wavelength = 400000.0f;
    broad.fractal.amplitude = 3000.0f;
    broad.fractal.octaves = 6;
    const auto ages = std::make_shared<terraingen::UniformAges>(4.0);
    terraingen::CraterLayerOptions options;
    options.generate = false;
    options.surface = moon;
    options.generation.manual.Append({0.0, 0.0, 300000.0, 3.9, 0u, true});
    options.generation.manual.Append({-60.0, 30.0, 200000.0, 3.5, 0u, true});

    terraingen::Terrain terrain;
    terrain.surface = moon;
    terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0);
    terrain.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{broad}},
                                                  terraingen::Vec2d{}, moon),
        std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(1e-15, 2.0), ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         ages, terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            options)};

    const terraingen::TileScheme scheme = terraingen::TileScheme::WholeBody(64);
    terraingen::TileCache cache(terrain, scheme, std::size_t{64} << 20);
    const terraingen::GeoKeys keys = terraingen::GeographicSphereKeys(moon.Radius());
    for (int level = 0; level <= 2; ++level) {
        const std::filesystem::path folder =
            output / "example_tiles_body_pyramid" / std::to_string(level);
        std::filesystem::create_directories(folder);
        const auto start = std::chrono::steady_clock::now();
        const auto tiles = terraingen::TileScheme::BodyTiles(level);
        for (const terraingen::TileKey& key : tiles) {
            terraingen::WriteGeoTiff(
                folder / (std::to_string(key.x) + "_" + std::to_string(key.y) + ".tif"),
                cache.Get(key)->heights, keys);
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        std::printf("level %d: %zu tiles at %.0f km spacing in %.2f s\n", level, tiles.size(),
                    scheme.Spacing(level) * moon.UnitLength() / 1000.0, elapsed.count());
    }
    std::printf("%zu kB held, %zu evictions\n", cache.Size() >> 10, cache.Evictions());
    return 0;
}
