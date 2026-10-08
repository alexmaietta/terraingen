/// The Moon on a sphere: a site at Mare Tranquillitatis and a mesh of the whole body.
///
/// On a sphere surface, grid coordinates are degrees of longitude and latitude while pixel sizes,
/// wavelengths and crater diameters stay in metres; noise is 3D at each point's position and
/// craters are placed by longitude and latitude. The site is a 51 km grid of 200 m cells; the
/// whole-body mesh is a cube-sphere whose vertices sit at the radius plus the terrain's height.
/// Writes the site's heights (with a geographic CRS for the Moon's sphere) and the mesh as glTF.
///
///     example_planets_moon [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const terraingen::Surface moon = terraingen::BodySphere(terraingen::Body::Moon);
    terraingen::NoiseLayer broad;
    broad.fractal.seed = 8;
    broad.fractal.wavelength = 400000.0f;
    broad.fractal.amplitude = 3000.0f;
    broad.fractal.octaves = 6;
    terraingen::NoiseLayer fine = broad;
    fine.fractal.seed = 9;
    fine.fractal.wavelength = 20000.0f;
    fine.fractal.amplitude = 300.0f;
    const auto noise = std::make_shared<terraingen::NoiseRelief>(
        terraingen::SyntheticTerrainParams{{broad, fine}}, terraingen::Vec2d{}, moon);

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.8);
    terraingen::CraterLayerOptions options;
    options.seed = 3;
    options.surface = moon;
    options.generation.dMin = 1000.0;
    options.generation.dMax = 30000.0;
    options.generation.manual.Append({0.0, 0.0, 300000.0, 3.9, 0u, true});
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{surface.density, surface.ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     surface.ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);

    const terraingen::Vec2d site{23.47, 0.67};
    const double pixel = 200.0;
    terraingen::Terrain terrain;
    terrain.surface = moon;
    terrain.source =
        std::make_shared<terraingen::FlatSource>(257, 257, pixel / moon.UnitLength(), 0.0f, site);
    terrain.layers = {noise, craters};

    std::printf("Moon radius %.1f km, %.1f m per degree\n", moon.Radius() / 1000.0,
                moon.UnitLength());
    const terraingen::Vec2d east = moon.Locate(site, {10000.0, 0.0});
    std::printf("10 km east of the site: (%.4f, %.4f)\n", east.x, east.y);
    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    std::printf("%zu craters; height at the site %.1f m\n", evaluated.features.craters.Size(),
                static_cast<double>(terraingen::EvaluateAt(terrain, site.x, site.y, pixel)));
    terraingen::WriteGeoTiff(output / "example_planets_moon_site.tif", evaluated.heights,
                             terraingen::GeographicSphereKeys(moon.Radius()));

    terraingen::Terrain global = terrain;
    global.layers = {
        noise, std::make_shared<terraingen::CraterLayer>(
                   terraingen::CraterModels{surface.density, surface.ages,
                                            std::make_shared<terraingen::NoDegradation>(
                                                terraingen::LunarFreshCraterMorphometry),
                                            std::make_shared<terraingen::FassettThomsonProfile>()},
                   [&] {
                       terraingen::CraterLayerOptions basins = options;
                       basins.generate = false;
                       return basins;
                   }())};
    const terraingen::TriangleMesh mesh = terraingen::BodyMesh(global, 64);
    double lowest = moon.Radius();
    double highest = moon.Radius();
    for (const terraingen::Vec3d& p : mesh.positions) {
        const double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        lowest = std::min(lowest, r);
        highest = std::max(highest, r);
    }
    std::printf("whole-body mesh: %zu vertices, radius %.2f to %.2f km\n", mesh.positions.size(),
                lowest / 1000.0, highest / 1000.0);
    terraingen::WriteGlb(output / "example_planets_moon.glb", mesh);
    return 0;
}
