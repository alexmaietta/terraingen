// SPDX-License-Identifier: BSD-3-Clause

#include "terrains.h"

#include "support/golombek_rapp.h"
#include "support/mesh_measures.h"
#include "support/raster_equality.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::EvaluationOptions;
using terraingen::GeoTransform;
using terraingen::LayerPtr;
using terraingen::RasterGrid;
using terraingen::Surface;
using terraingen::Terrain;
using terraingen::testing::Datum;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Rolling;
using terraingen::testing::Smoothed;

namespace {

/// The 16 x 16 cells of `grid` from column `col0` and row `row0`.
RasterGrid Window(const RasterGrid& grid, std::size_t col0, std::size_t row0) {
    return terraingen::Subgrid(grid, {col0, col0 + 16, row0, row0 + 16});
}

/// Expects `window`, the heights over the 16 x 16 cells of a larger grid from (`col0`, `row0`), to
/// equal `whole`'s there to within `tolerance`.
void ExpectSameCells(const terraingen::Raster<float>& window,
                     const terraingen::Raster<float>& whole, std::size_t col0, std::size_t row0,
                     float tolerance = 0.0f) {
    for (std::size_t row = 0; row < window.Height(); ++row) {
        for (std::size_t col = 0; col < window.Width(); ++col) {
            ASSERT_NEAR(window(col, row), whole(col0 + col, row0 + row), tolerance)
                << "window at " << col0 << "," << row0 << " cell " << col << "," << row;
        }
    }
}

/// Fresh lunar crater models with a power-law density of `coefficient` D^-2.
terraingen::CraterModels LunarModels(double coefficient) {
    return terraingen::CraterModels{
        std::make_shared<terraingen::PowerLawSfd>(coefficient, 2.0),
        std::make_shared<terraingen::UniformAges>(1.0),
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
        std::make_shared<terraingen::FassettThomsonProfile>()};
}

/// A terrain on `surface` over a flat 8 x 8 source at `elevation` with `layers`.
Terrain OnBody(const Surface& surface, float elevation, std::vector<LayerPtr> layers = {}) {
    return Terrain(surface, std::make_shared<terraingen::FlatSource>(8, 8, 1.0, elevation),
                   std::move(layers));
}

}  // namespace

TEST(Evaluate, AWindowEqualsTheSameCellsOfALargerGridWithRoundedRims) {
    const Terrain terrain = Smoothed();
    const RasterGrid large{200, 160, GeoTransform::NorthUp(-200.0, 160.0, 2.0)};
    const auto whole = terrain.Evaluate(large);
    for (const auto& [col0, row0] :
         {std::pair<std::size_t, std::size_t>{68, 48}, std::pair<std::size_t, std::size_t>{84, 32},
          std::pair<std::size_t, std::size_t>{52, 64}}) {
        const auto window =
            terrain.Evaluate(Window(large, col0, row0), EvaluationOptions{}.Threads(4));
        ExpectSameCells(window, whole, col0, row0);
    }
}

TEST(Evaluate, CratersCutToADatumEqualALargerGridInAWindow) {
    const Terrain terrain = Datum();
    const RasterGrid large{120, 100, GeoTransform::NorthUp(-120.0, 100.0, 2.0)};
    const auto whole = terrain.Evaluate(large, EvaluationOptions{}.Threads(4));
    ExpectSameRaster(whole, terrain.Evaluate(large, EvaluationOptions{}.Threads(1)));
    for (const auto& [col0, row0] : {std::pair<std::size_t, std::size_t>{28, 34},
                                     std::pair<std::size_t, std::size_t>{12, 18}}) {
        ExpectSameCells(terrain.Evaluate(Window(large, col0, row0)), whole, col0, row0);
    }
}

TEST(Evaluate, BouldersFromCraterEjectaAndBedrockAgreeOnGridsWindowsAndPoints) {
    const Terrain rolling = Rolling();
    const auto craters =
        std::dynamic_pointer_cast<const terraingen::CraterLayer>(rolling.Layers()[1]);
    ASSERT_NE(craters, nullptr);
    terraingen::BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.minDiameter = 1.0;
    options.sizes.coverage = 0.05;
    options.patches = {2.0, 60.0};
    options.ejecta.coverage = 0.15;
    options.ejecta.minCraterDiameter = 20.0;
    options.ejecta.minFreshness = 0.0;
    options.bedrock.exposure = terraingen::ExposureParams{};
    options.fade = {1.0, 2.0};
    std::vector<LayerPtr> layers = rolling.Layers();
    layers.push_back(std::make_shared<terraingen::BoulderLayer>(options, craters));
    const Terrain terrain(rolling.Surface(), rolling.Source(), layers);

    const RasterGrid large{240, 200, GeoTransform::NorthUp(-120.0, 100.0, 1.0)};
    const auto whole = terrain.Evaluate(large, EvaluationOptions{}.Threads(4));
    EXPECT_FALSE(terrain.Boulders(large).boulders.Empty());
    for (std::size_t row = 3; row < large.height; row += 7) {
        for (std::size_t col = 1; col < large.width; col += 7) {
            const auto center = whole.CellCenter(col, row);
            ASSERT_EQ(terrain.HeightAt(center.x, center.y, 1.0), whole(col, row));
        }
    }
    ExpectSameCells(terrain.Evaluate(Window(large, 56, 68)), whole, 56, 68);
}

TEST(Evaluate, SphereWindowsAndPointsEqualTheWholeTerrain) {
    const Surface moon = Surface::Sphere(1737400.0);
    const double step = 4.0 / moon.UnitLength();
    std::vector<terraingen::Fractal> noise{terraingen::Fractal{}};
    noise[0].wavelength = 400.0f;
    noise[0].amplitude = 6.0f;
    terraingen::CraterLayerOptions options;
    options.seed = 4;
    options.generation.dMin = 12.0;
    options.generation.dMax = 80.0;
    options.apply.rimSmoothing.sigmaRadii = 0.05;
    const Terrain terrain(
        moon, std::make_shared<terraingen::FlatSource>(96, 64, step, 0.0f),
        {std::make_shared<terraingen::NoiseLayer>(noise),
         std::make_shared<terraingen::CraterLayer>(LunarModels(0.05), options, moon)});
    const RasterGrid grid = terrain.Grid().value();
    const auto whole = terrain.Evaluate(grid);
    EXPECT_GT(terrain.Craters(grid).craters.Size(), 3u);
    for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{10, 10},
                                   std::pair<std::size_t, std::size_t>{50, 33}}) {
        const auto center = whole.CellCenter(col, row);
        EXPECT_EQ(terrain.HeightAt(center.x, center.y, 4.0), whole(col, row));
    }
    const RasterGrid large{200, 160, GeoTransform::NorthUp(-100.0 * step, 80.0 * step, step)};
    const auto smoothed = terrain.Evaluate(large);
    for (const auto& [col0, row0] : {std::pair<std::size_t, std::size_t>{84, 64},
                                     std::pair<std::size_t, std::size_t>{100, 48}}) {
        const auto window =
            terrain.Evaluate(Window(large, col0, row0), EvaluationOptions{}.Threads(2));
        ExpectSameCells(window, smoothed, col0, row0, 1e-3f);
    }
}

TEST(Evaluate, WholeBodyGridsAgreeAcrossTheAntimeridianAndAtThePole) {
    const Surface moon = Surface::Sphere(1737400.0);
    terraingen::CraterLayerOptions options;
    options.seed = 6;
    options.generation.dMin = 300.0;
    options.generation.dMax = 1500.0;
    options.apply.rimSmoothing.sigmaRadii = 0.05;
    const Terrain terrain = OnBody(
        moon, 0.0f, {std::make_shared<terraingen::CraterLayer>(LunarModels(0.5), options, moon)});
    const double size = 180.0 / 512.0;
    const double step = size / 16.0;
    const auto square = [&](double west, double north) {
        return RasterGrid{16, 16, GeoTransform::NorthUp(west, north, step)};
    };
    const auto two = EvaluationOptions{}.Threads(2);
    for (const auto& [west, north] : {std::pair<double, double>{180.0 - size, 90.0 - 100.0 * size},
                                      std::pair<double, double>{-180.0, 90.0 - 100.0 * size},
                                      std::pair<double, double>{-180.0 + 37.0 * size, 90.0}}) {
        const auto inner = terrain.Evaluate(square(west, north), two);
        const RasterGrid around{
            48, 48, GeoTransform::NorthUp(west - 16.0 * step, north + 16.0 * step, step)};
        ExpectSameCells(inner, terrain.Evaluate(around), 16, 16, 1e-3f);
    }
    const auto east = terrain.Evaluate(square(180.0 - size, 90.0 - 100.0 * size));
    const auto west = terrain.Evaluate(square(-180.0, 90.0 - 100.0 * size));
    const RasterGrid shifted{32, 16,
                             GeoTransform::NorthUp(180.0 - size, 90.0 - 100.0 * size, step)};
    const auto across = terrain.Evaluate(shifted);
    double relief = 0.0;
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            EXPECT_NEAR(across(col, row), east(col, row), 1e-3);
            EXPECT_NEAR(across(col + 16, row), west(col, row), 1e-3);
            relief = std::fmax(relief, std::fabs(east(col, row)));
        }
    }
    EXPECT_GT(relief, 1.0);
}

TEST(BodyMesh, CoversTheBodyWithOutwardTrianglesAtTheTerrainsHeights) {
    const Surface moon = Surface::Sphere(1737400.0);
    std::vector<terraingen::Fractal> noise{terraingen::Fractal{}};
    noise[0].wavelength = 300000.0f;
    noise[0].amplitude = 2000.0f;
    const Terrain terrain = OnBody(moon, 100.0f, {std::make_shared<terraingen::NoiseLayer>(noise)});
    const std::size_t n = 8;
    const terraingen::TriangleMesh mesh = terraingen::BodyMesh(terrain, n, 0.0, 2);
    ASSERT_EQ(mesh.positions.size(), 6 * (n + 1) * (n + 1));
    ASSERT_EQ(mesh.triangles.size(), 6 * n * n * 2);
    const double step = 1737400.0 * (terraingen::kPi / 2.0) / static_cast<double>(n);
    for (std::size_t k = 0; k < mesh.positions.size(); k += 7) {
        const auto& p = mesh.positions[k];
        const double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const auto point = moon.Coordinates(p);
        const double height = static_cast<double>(terrain.HeightAt(point.x, point.y, step));
        EXPECT_NEAR(r, 1737400.0 + height, 1e-3);
    }
    for (const auto& t : mesh.triangles) {
        const auto& a = mesh.positions[t[0]];
        const auto& b = mesh.positions[t[1]];
        const auto& c = mesh.positions[t[2]];
        const terraingen::Vec3d u{b.x - a.x, b.y - a.y, b.z - a.z};
        const terraingen::Vec3d v{c.x - a.x, c.y - a.y, c.z - a.z};
        const terraingen::Vec3d normal{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                                       u.x * v.y - u.y * v.x};
        EXPECT_GT(normal.x * a.x + normal.y * a.y + normal.z * a.z, 0.0);
    }
    const auto corner = [&](std::size_t face, std::size_t i, std::size_t j) {
        return mesh.positions[face * (n + 1) * (n + 1) + j * (n + 1) + i];
    };
    const auto pxEdge = corner(0, n, n / 2);
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t k = 2 * (n + 1) * (n + 1); k < 3 * (n + 1) * (n + 1); ++k) {
        const auto& q = mesh.positions[k];
        nearest = std::fmin(nearest, std::hypot(q.x - pxEdge.x, q.y - pxEdge.y, q.z - pxEdge.z));
    }
    EXPECT_LT(nearest, 1e-3);

    const Terrain flat(Surface{}, terrain.Source(), terrain.Layers());
    EXPECT_THROW(terraingen::BodyMesh(flat, 4), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyMesh(terrain, 0), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyMesh(terrain, 4, -1.0), std::invalid_argument);
    EXPECT_NO_THROW(terraingen::BodyMesh(terrain, 2));
}

TEST(BodyMesh, PlacesAFlatTerrainOnItsEllipsoid) {
    const Terrain terrain = OnBody(Surface::Ellipsoid(270.0, 147.0, 104.0), 0.0f);
    const terraingen::TriangleMesh mesh = terraingen::BodyMesh(terrain, 6, 0.0, 2);
    for (const auto& p : mesh.positions) {
        EXPECT_NEAR(p.x * p.x / (270.0 * 270.0) + p.y * p.y / (147.0 * 147.0) +
                        p.z * p.z / (104.0 * 104.0),
                    1.0, 1e-9);
    }
}

TEST(BodyIcq, PlacesEachLabelAtTheBodysRadiusPlusTheHeight) {
    const Surface body = Surface::Ellipsoid(270.0, 147.0, 104.0);
    const Terrain terrain = OnBody(body, 3.0f);
    const terraingen::IcqModel model = terraingen::BodyIcq(terrain, 6, 0.0, 2);
    ASSERT_EQ(model.q, 6u);
    ASSERT_EQ(model.vertices.size(), 6u * 7u * 7u);
    for (std::size_t f = 1; f <= 6; ++f) {
        for (std::size_t j = 0; j <= 6; ++j) {
            for (std::size_t i = 0; i <= 6; ++i) {
                const terraingen::Vec3d p = model.vertices[model.Index(i, j, f)];
                const terraingen::Vec3d d = terraingen::IcqDirection(6, i, j, f);
                const terraingen::Vec2d point = body.Coordinates(d);
                const float height =
                    terrain.HeightAt(point.x, point.y, body.Radius() * terraingen::kPi / 2.0 / 6.0);
                EXPECT_NEAR(terraingen::Length(p),
                            body.RadiusAt(point) +
                                (std::isnan(height) ? 0.0 : static_cast<double>(height)),
                            1e-9);
            }
        }
    }
    EXPECT_GT(terraingen::testing::Volume(terraingen::IcqToMesh(model)), 0.0);
    const Terrain flat(Surface{}, terrain.Source(), {});
    EXPECT_THROW(terraingen::BodyIcq(flat, 4), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyIcq(terrain, 0), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyIcq(terrain, 4, -1.0), std::invalid_argument);
}
