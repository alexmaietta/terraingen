#include "terrains.h"

#include "support/raster_equality.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

using terraingen::Evaluate;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::Terrain;
using terraingen::testing::Datum;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Rolling;
using terraingen::testing::Smoothed;

TEST(EvaluateTile, EqualsTheSameCellsOfALargerGridWithFilters) {
    const Terrain terrain = Smoothed();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const double margin = terraingen::FilterMargin(terrain, 2.0);
    EXPECT_DOUBLE_EQ(margin, (1.2 + 3.0 * 0.05) * 25.0 + 4.0);
    const RasterGrid large{200, 160, GeoTransform::NorthUp(-200.0, 160.0, 2.0)};
    const auto whole = Evaluate(terrain, large);
    for (const terraingen::TileKey key :
         {terraingen::TileKey{2, 1, 1}, terraingen::TileKey{2, 2, 0},
          terraingen::TileKey{2, 0, 2}}) {
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key, 4);
        const RasterGrid grid = scheme.Grid(key);
        const auto offset =
            large.transform.WorldToPixel(grid.transform.originX, grid.transform.originY);
        const auto col0 = static_cast<std::size_t>(offset.x);
        const auto row0 = static_cast<std::size_t>(offset.y);
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                ASSERT_EQ(tile(col, row), whole(col0 + col, row0 + row))
                    << "tile " << key.x << "," << key.y << " cell " << col << "," << row;
            }
        }
    }
}

TEST(EvaluateTile, MatchesGridsEvaluatedAtEachLevelsSpacing) {
    const Terrain terrain = Rolling();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    for (const int level : {0, 1, 2, 3}) {
        const terraingen::TileKey key{level, 0, 0};
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key);
        const auto grid = Evaluate(terrain, scheme.Grid(key));
        ExpectSameRaster(tile, grid);
    }
    EXPECT_EQ(terraingen::FilterMargin(terrain, 2.0), 0.0);
}

TEST(EvaluateTiles, EvaluatesEveryTileInOrder) {
    const Terrain terrain = Smoothed();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const auto keys = scheme.TilesCovering({-96.0, 0.0, 0.0, 64.0}, 2);
    const auto tiles = terraingen::EvaluateTiles(terrain, scheme, keys, 4);
    ASSERT_EQ(tiles.size(), keys.size());
    for (std::size_t n = 0; n < keys.size(); ++n) {
        ExpectSameRaster(tiles[n], terraingen::EvaluateTile(terrain, scheme, keys[n]));
    }
    EXPECT_THROW(
        terraingen::EvaluateTile(terrain, terraingen::TileScheme{{0.0, 0.0}, -1.0, 16}, {0, 0, 0}),
        std::invalid_argument);
}

TEST(EvaluateTile, CratersCutToADatumEqualALargerGrid) {
    const Terrain terrain = Datum();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const RasterGrid large{120, 100, GeoTransform::NorthUp(-120.0, 100.0, 2.0)};
    const auto whole = Evaluate(terrain, large, terraingen::EvaluationOptions{0.0, 4});
    ExpectSameRaster(whole, Evaluate(terrain, large, terraingen::EvaluationOptions{0.0, 1}));
    for (const terraingen::TileKey key :
         {terraingen::TileKey{2, 1, 1}, terraingen::TileKey{2, 0, 0}}) {
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key);
        const RasterGrid grid = scheme.Grid(key);
        const auto offset =
            large.transform.WorldToPixel(grid.transform.originX, grid.transform.originY);
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                ASSERT_EQ(tile(col, row), whole(static_cast<std::size_t>(offset.x) + col,
                                                static_cast<std::size_t>(offset.y) + row));
            }
        }
    }
}

TEST(Evaluate, BouldersFromCraterEjectaAndBedrockAgreeOnGridsTilesAndPoints) {
    Terrain terrain = Rolling();
    const auto craters =
        std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]);
    ASSERT_NE(craters, nullptr);
    terraingen::BoulderLayerOptions options;
    options.sizes.minDiameter = 1.0;
    options.sizes.coverage = 0.05;
    options.patches = {2.0, 60.0};
    options.ejecta.coverage = 0.15;
    options.ejecta.minCraterDiameter = 20.0;
    options.ejecta.minFreshness = 0.0;
    options.bedrock.exposure = terraingen::ExposureParams{};
    options.fade = {1.0, 2.0};
    terrain.layers.push_back(std::make_shared<terraingen::BoulderLayer>(options, craters));

    const RasterGrid large{240, 200, GeoTransform::NorthUp(-120.0, 100.0, 1.0)};
    const auto whole = Evaluate(terrain, large, terraingen::EvaluationOptions{0.0, 4});
    EXPECT_FALSE(terraingen::PlacedBoulders(terrain, large).boulders.empty());
    for (std::size_t row = 3; row < large.height; row += 7) {
        for (std::size_t col = 1; col < large.width; col += 7) {
            const auto centre = whole.CellCenter(col, row);
            ASSERT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, 1.0), whole(col, row));
        }
    }
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const terraingen::TileKey key{3, 2, 2};
    const auto tile = terraingen::EvaluateTile(terrain, scheme, key);
    const RasterGrid grid = scheme.Grid(key);
    const auto offset =
        large.transform.WorldToPixel(grid.transform.originX, grid.transform.originY);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            ASSERT_EQ(tile(col, row), whole(static_cast<std::size_t>(offset.x) + col,
                                            static_cast<std::size_t>(offset.y) + row));
        }
    }
}

TEST(Evaluate, SphereTilesAndPointsEqualTheWholeTerrain) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const double step = 4.0 / moon.UnitLength();
    Terrain terrain;
    terrain.surface = moon;
    terrain.source = std::make_shared<terraingen::FlatSource>(96, 64, step, 0.0f);
    terraingen::SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 400.0f;
    noise.layers[0].fractal.amplitude = 6.0f;
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(noise, terraingen::Vec2d{}, moon));
    terraingen::CraterLayerOptions options;
    options.seed = 4;
    options.surface = moon;
    options.generation.dMin = 12.0;
    options.generation.dMax = 80.0;
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options));
    const auto whole = Evaluate(terrain, terrain.source->Grid());
    EXPECT_GT(terraingen::PlacedCraters(terrain, terrain.source->Grid()).craters.Size(), 3u);
    for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{10, 10},
                                   std::pair<std::size_t, std::size_t>{50, 33}}) {
        const auto centre = whole.CellCenter(col, row);
        EXPECT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, 4.0), whole(col, row));
    }
    terrain.filters.push_back(terraingen::testing::RimFilter(terrain));
    const RasterGrid large{200, 160, GeoTransform::NorthUp(-100.0 * step, 80.0 * step, step)};
    const auto smoothed = Evaluate(terrain, large);
    const terraingen::TileScheme scheme{{-48.0 * step, 32.0 * step}, 64.0 * step, 16};
    for (const terraingen::TileKey key :
         {terraingen::TileKey{2, 1, 1}, terraingen::TileKey{2, 2, 0}}) {
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key, 2);
        const RasterGrid grid = scheme.Grid(key);
        const auto offset =
            large.transform.WorldToPixel(grid.transform.originX, grid.transform.originY);
        const auto col0 = static_cast<std::size_t>(std::lround(offset.x));
        const auto row0 = static_cast<std::size_t>(std::lround(offset.y));
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                ASSERT_NEAR(tile(col, row), smoothed(col0 + col, row0 + row), 1e-3)
                    << col << " " << row;
            }
        }
    }
}

TEST(EvaluateTile, WholeBodyTilesAgreeAcrossTheAntimeridianAndAtThePole) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const auto scheme = terraingen::TileScheme::WholeBody(16);
    EXPECT_EQ(scheme.TileSize(0), 180.0);
    EXPECT_EQ(terraingen::TileScheme::BodyTiles(2).size(), 32u);
    EXPECT_EQ(terraingen::TileScheme::WrapBodyTile({3, -1, 2}), (terraingen::TileKey{3, 15, 2}));
    EXPECT_EQ(terraingen::TileScheme::WrapBodyTile({3, 16, 2}), (terraingen::TileKey{3, 0, 2}));
    EXPECT_THROW(terraingen::TileScheme::BodyTiles(-1), std::invalid_argument);

    Terrain terrain;
    terrain.surface = moon;
    terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0, 0.0f);
    terraingen::CraterLayerOptions options;
    options.seed = 6;
    options.surface = moon;
    options.generation.dMin = 300.0;
    options.generation.dMax = 1500.0;
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.5, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options));
    terrain.filters.push_back(terraingen::testing::RimFilter(terrain));
    const int level = 9;
    const std::int64_t columns = std::int64_t{2} << level;
    const double size = scheme.TileSize(level);
    const double step = size / 16.0;
    for (const terraingen::TileKey key :
         {terraingen::TileKey{level, columns - 1, 100}, terraingen::TileKey{level, 0, 100},
          terraingen::TileKey{level, 37, 0}}) {
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key, 2);
        const RasterGrid grid = scheme.Grid(key);
        const RasterGrid around{48, 48,
                                GeoTransform::NorthUp(grid.transform.originX - 16.0 * step,
                                                      grid.transform.originY + 16.0 * step, step)};
        const auto larger = Evaluate(terrain, around);
        for (std::size_t row = 0; row < 16; ++row) {
            for (std::size_t col = 0; col < 16; ++col) {
                ASSERT_NEAR(tile(col, row), larger(col + 16, row + 16), 1e-3)
                    << key.x << " " << key.y << " " << col << " " << row;
            }
        }
    }
    const auto east = terraingen::EvaluateTile(terrain, scheme, {level, columns - 1, 100}, 1);
    const RasterGrid shifted{32, 16,
                             GeoTransform::NorthUp(180.0 - size, 90.0 - 100.0 * size, step)};
    const auto across = Evaluate(terrain, shifted);
    const auto west = terraingen::EvaluateTile(terrain, scheme, {level, 0, 100}, 1);
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
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    Terrain terrain;
    terrain.surface = moon;
    terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0, 100.0f);
    terraingen::SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 300000.0f;
    noise.layers[0].fractal.amplitude = 2000.0f;
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(noise, terraingen::Vec2d{}, moon));
    const std::size_t n = 8;
    const terraingen::TriangleMesh mesh = terraingen::BodyMesh(terrain, n, 0.0, 2);
    ASSERT_EQ(mesh.positions.size(), 6 * (n + 1) * (n + 1));
    ASSERT_EQ(mesh.triangles.size(), 6 * n * n * 2);
    const double step = 1737400.0 * (M_PI / 2.0) / static_cast<double>(n);
    for (std::size_t k = 0; k < mesh.positions.size(); k += 7) {
        const auto& p = mesh.positions[k];
        const double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const auto point = moon.PointOf(p);
        const double height = terraingen::EvaluateAt(terrain, point.x, point.y, step);
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
    double nearest = INFINITY;
    for (std::size_t k = 2 * (n + 1) * (n + 1); k < 3 * (n + 1) * (n + 1); ++k) {
        const auto& q = mesh.positions[k];
        nearest = std::fmin(nearest, std::hypot(q.x - pxEdge.x, q.y - pxEdge.y, q.z - pxEdge.z));
    }
    EXPECT_LT(nearest, 1e-3);

    Terrain flat = terrain;
    flat.surface = {};
    EXPECT_THROW(terraingen::BodyMesh(flat, 4), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyMesh(terrain, 0), std::invalid_argument);
    EXPECT_THROW(terraingen::BodyMesh(terrain, 4, -1.0), std::invalid_argument);
    terrain.filters.push_back(terraingen::testing::RimFilter(terrain));
    EXPECT_NO_THROW(terraingen::BodyMesh(terrain, 2));
}
