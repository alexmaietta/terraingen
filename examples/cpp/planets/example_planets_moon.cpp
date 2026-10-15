/// The Moon on a sphere: a site at Mare Tranquillitatis and a mesh of the whole body.
///
/// On a sphere surface, grid coordinates are degrees of longitude and latitude while pixel sizes,
/// wavelengths and crater diameters stay in metres; noise is 3D at each point's position and
/// craters are placed by longitude and latitude. The site is a 51 km grid of 200 m cells; the
/// whole-body mesh is a cube-sphere whose vertices sit at the radius plus the terrain's height.
/// Writes the site's heights (with a geographic CRS for the Moon's sphere) and the mesh as glTF.
///
///     example_planets_moon [--output-dir DIR] [--test]
///     terraingen-examples planets_moon

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>

namespace {

class PlanetsMoon : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
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
            terraingen::SyntheticTerrainParams{{broad, fine}}, terraingen::Vec2d{}, moon_);

        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.8);
        terraingen::CraterLayerOptions options;
        options.seed = 3;
        options.surface = moon_;
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
        terrain.surface = moon_;
        terrain.source = std::make_shared<terraingen::FlatSource>(
            257, 257, pixel / moon_.UnitLength(), 0.0f, site);
        terrain.layers = {noise, craters};

        Out() << std::fixed << std::setprecision(1) << "Moon radius " << moon_.Radius() / 1000.0
              << " km, " << moon_.UnitLength() << " m per degree\n";
        east_ = moon_.Locate(site, {10000.0, 0.0});
        Out() << std::setprecision(4) << "10 km east of the site: (" << east_.x << ", " << east_.y
              << ")\n";
        site_ = terraingen::Evaluate(terrain);
        siteHeight_ = terraingen::EvaluateAt(terrain, site.x, site.y, pixel);
        Out() << std::setprecision(1) << site_.features.craters.Size()
              << " craters; height at the site " << siteHeight_ << " m\n";

        terraingen::Terrain global = terrain;
        global.layers = {noise, std::make_shared<terraingen::CraterLayer>(
                                    terraingen::CraterModels{
                                        surface.density, surface.ages,
                                        std::make_shared<terraingen::NoDegradation>(
                                            terraingen::LunarFreshCraterMorphometry),
                                        std::make_shared<terraingen::FassettThomsonProfile>()},
                                    [&] {
                                        terraingen::CraterLayerOptions basins = options;
                                        basins.generate = false;
                                        return basins;
                                    }())};
        mesh_ = terraingen::BodyMesh(global, 64);
        lowest_ = moon_.Radius();
        highest_ = moon_.Radius();
        for (const terraingen::Vec3d& p : mesh_.positions) {
            const double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            lowest_ = std::min(lowest_, r);
            highest_ = std::max(highest_, r);
        }
        Out() << std::setprecision(2) << "whole-body mesh: " << mesh_.positions.size()
              << " vertices, radius " << lowest_ / 1000.0 << " to " << highest_ / 1000.0 << " km\n"
              << std::defaultfloat;
        Show("site", site_.heights, nullptr, true);
    }

    void Save(const std::filesystem::path& dir) override {
        terraingen::WriteGeoTiff(dir / "example_planets_moon_site.tif", site_.heights,
                                 terraingen::GeographicSphereKeys(moon_.Radius()));
        terraingen::WriteGlb(dir / "example_planets_moon.glb", mesh_);
    }

    void Test() override {
        Check(site_.heights.Width() == 257 && site_.heights.Height() == 257,
              "the site is 257 x 257");
        Check(std::abs(moon_.Radius() - 1737400.0) < 1000.0, "the Moon's radius is 1737 km");
        Check(east_.x > 23.6 && east_.x < 24.0 && std::abs(east_.y - 0.67) < 0.01,
              "10 km east of the site is about a third of a degree east, at the same latitude");
        Check(site_.features.craters.Size() > 10, "more than 10 craters at the site");
        Check(std::isfinite(siteHeight_) && std::abs(siteHeight_) < 10000.0,
              "the site's height is finite and within 10 km of the datum");
        Check(mesh_.positions.size() > 10000 && !mesh_.triangles.empty() &&
                  lowest_ > moon_.Radius() - 20000.0 && highest_ < moon_.Radius() + 20000.0 &&
                  highest_ > lowest_,
              "the whole-body mesh has relief within 20 km of the radius");
    }

  private:
    terraingen::Surface moon_ = terraingen::BodySphere(terraingen::Body::Moon);
    terraingen::Vec2d east_;
    terraingen::EvaluatedTerrain site_;
    float siteHeight_ = 0.0f;
    terraingen::TriangleMesh mesh_;
    double lowest_ = 0.0;
    double highest_ = 0.0;
};

}  // namespace

TERRAINGEN_EXAMPLE(PlanetsMoon, "The Moon on a sphere: a site and a whole-body mesh")
