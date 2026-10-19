#include "terraingen/pipeline/nested.h"

#include "terraingen/layers/midpoint_displacement.h"
#include "terraingen/layers/noise_relief.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::GeoTransform;
using terraingen::NestedLevel;
using terraingen::NestedRegion;
using terraingen::NestedRegionError;
using terraingen::NestedTerrain;
using terraingen::RasterGrid;

namespace {

/// 65 x 65 cells of 2 m centred on the origin.
RasterGrid Base() {
    return {65, 65, GeoTransform::NorthUp(-65.0, 65.0, 2.0)};
}

/// A region of two levels, 1 m over 40 m and 0.5 m over 20 m, about (13.3, -7.9).
NestedRegion Region(const std::string& name = "site") {
    NestedRegion region;
    region.name = name;
    region.centre = {13.3, -7.9};
    region.levels = terraingen::RepeatedLevels(2.0, 40.0, 2);
    return region;
}

/// Gentle noise relief.
terraingen::Terrain Hills() {
    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 60.0f;
    layer.fractal.amplitude = 5.0f;
    layer.fractal.octaves = 3;
    terraingen::Terrain terrain;
    terraingen::SyntheticTerrainParams params;
    params.layers = {layer};
    terrain.layers = {std::make_shared<terraingen::NoiseRelief>(params)};
    return terrain;
}

/// Midpoint displacement of zero on the base's cells.
std::shared_ptr<const terraingen::Layer> Upscaling() {
    terraingen::MidpointDisplacementOptions options;
    options.pixelSize = 2.0;
    options.origin = {-64.0, 64.0};
    options.scale = 0.3;
    options.base = terraingen::MidpointBase::Source;
    return std::make_shared<terraingen::MidpointDisplacementLayer>(options);
}

/// The index of the region whose problem `run` throws, or -1.
template <typename Run> int FailingRegion(Run run) {
    try {
        run();
    } catch (const NestedRegionError& error) {
        return static_cast<int>(error.Region());
    }
    return -1;
}

}  // namespace

TEST(RepeatedLevels, RefineByTheFactorAndNarrowByTheShrink) {
    EXPECT_EQ(terraingen::RepeatedLevels(4.0, 100.0, 3, 2, 2.0),
              (std::vector<NestedLevel>{{2.0, 100.0}, {1.0, 50.0}, {0.5, 25.0}}));
    EXPECT_EQ(terraingen::RepeatedLevels(9.0, 30.0, 2, 3, 1.0),
              (std::vector<NestedLevel>{{3.0, 30.0}, {1.0, 30.0}}));
    EXPECT_TRUE(terraingen::RepeatedLevels(1.0, 1.0, 0).empty());
    EXPECT_THROW(terraingen::RepeatedLevels(0.0, 1.0, 1), std::invalid_argument);
    EXPECT_THROW(terraingen::RepeatedLevels(1.0, -1.0, 1), std::invalid_argument);
    EXPECT_THROW(terraingen::RepeatedLevels(1.0, 1.0, 1, 1), std::invalid_argument);
    EXPECT_THROW(terraingen::RepeatedLevels(1.0, 1.0, 1, 2, 0.5), std::invalid_argument);
}

TEST(NestGrids, PutEachLevelsEdgesOnTheCellCentresAbove) {
    const auto grids = terraingen::NestGrids(Base(), {Region()});
    ASSERT_EQ(grids.size(), 3u);
    EXPECT_FALSE(grids[0].region.has_value());
    EXPECT_EQ(grids[0].grid, Base());
    EXPECT_EQ(grids[0].children, std::vector<std::size_t>{1u});
    EXPECT_EQ(grids[1].region, 0u);
    EXPECT_EQ(grids[1].level, 1u);
    EXPECT_EQ(grids[1].parent, 0u);
    EXPECT_EQ(grids[2].parent, 1u);
    EXPECT_EQ(grids[1].grid.width, 41u);
    EXPECT_EQ(grids[2].grid.width, 41u);
    const terraingen::Vec2d centre = grids[1].grid.transform.PixelToWorld(20.5, 20.5);
    EXPECT_DOUBLE_EQ(centre.x, 14.0) << "snapped to a base cell centre";
    EXPECT_DOUBLE_EQ(centre.y, -8.0);
    for (std::size_t g = 1; g < grids.size(); ++g) {
        const auto& t = grids[g].grid.transform;
        const auto& above = grids[*grids[g].parent].grid.transform;
        for (double offset : {t.PixelToWorld(0.5, 0.5).x - above.PixelToWorld(0.5, 0.5).x,
                              t.PixelToWorld(0.5, 0.5).y - above.PixelToWorld(0.5, 0.5).y}) {
            const double cells = offset / above.pixelWidth;
            EXPECT_NEAR(cells, std::round(cells), 1e-9);
        }
    }
}

TEST(NestGrids, RejectBadBasesAndNameTheRegionAtFault) {
    EXPECT_THROW(terraingen::NestGrids({10, 10, {0.0, 2.0, 0.0, 0.0, 0.0, -1.0}}, {}),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::NestGrids({10, 10, GeoTransform{0.0, 1.0, 0.0, 0.0, 0.0, 1.0}}, {}),
                 std::invalid_argument)
        << "south-up";
    const auto failing = [](std::vector<NestedRegion> regions) {
        return FailingRegion([&] { terraingen::NestGrids(Base(), regions); });
    };
    EXPECT_EQ(failing({Region(), Region("")}), 1);
    EXPECT_EQ(failing({Region(), Region()}), 1) << "two named site";
    NestedRegion empty = Region();
    empty.levels.clear();
    EXPECT_EQ(failing({empty}), 0);
    NestedRegion lost = Region();
    lost.centre.x = std::nan("");
    EXPECT_EQ(failing({lost}), 0);
    NestedRegion odd = Region();
    odd.levels[1].pixelSize = 0.4;
    EXPECT_EQ(failing({odd}), 0) << "1 m over 2.5";
    odd.levels[1].pixelSize = 1.0;
    EXPECT_EQ(failing({odd}), 0) << "not finer";
    odd.levels[1].pixelSize = 0.0;
    EXPECT_EQ(failing({odd}), 0);
    NestedRegion wide = Region();
    wide.centre = {50.0, 0.0};
    EXPECT_EQ(failing({wide}), 0);
    NestedRegion near = Region("near");
    near.centre = {-10.0, -5.0};
    EXPECT_EQ(failing({Region(), near}), 1);
    near.centre = {-40.0, 40.0};
    EXPECT_EQ(failing({Region(), near}), -1);
    NestedRegion thirds = Region();
    thirds.levels = {{2.0 / 3.0, 30.0}};
    EXPECT_EQ(terraingen::NestGrids(Base(), {thirds})[1].grid.width, 49u)
        << "a factor of 3: eight base cells each side";
}

TEST(NestedTerrain, NestsEachLevelOnTheCellsAboveAndBlendsItsEdges) {
    NestedRegion region = Region();
    region.upscaling = {Upscaling()};
    region.blendCells = 0;
    const NestedTerrain plain(Hills(), Base(), {region});
    EXPECT_EQ(plain.FindRegion("site"), 0u);
    EXPECT_FALSE(plain.FindRegion("other").has_value());
    EXPECT_EQ(plain.GridIndex(0, 0), 0u);
    EXPECT_EQ(plain.GridIndex(0, 2), 2u);
    EXPECT_THROW(plain.GridIndex(0, 3), std::out_of_range);
    EXPECT_THROW(plain.GridIndex(1, 0), std::out_of_range);
    EXPECT_EQ(plain.GridTerrain(0).layers.size(), 1u);
    EXPECT_EQ(plain.GridTerrain(2).layers.size(), 2u) << "upscaling, then the terrain's layers";
    EXPECT_EQ(plain.Site().x, 0.0);

    terraingen::EvaluationOptions options;
    options.threads = 2;
    const auto heights = plain.EvaluateAll(options);
    ASSERT_EQ(heights.size(), 3u);
    const terraingen::Vec2d first = heights[1].CellCenter(0, 0);
    const terraingen::Vec2d pixel = heights[0].Transform().WorldToPixel(first.x, first.y);
    const auto col0 = static_cast<std::size_t>(pixel.x);
    const auto row0 = static_cast<std::size_t>(pixel.y);
    for (std::size_t row = 0; row < 41; row += 2) {
        for (std::size_t col = 0; col < 41; col += 2) {
            ASSERT_EQ(heights[1](col, row), heights[0](col0 + col / 2, row0 + row / 2));
            ASSERT_EQ(heights[2](col, row), heights[1](10 + col / 2, 10 + row / 2));
        }
    }
    EXPECT_NE(heights[2](1, 1), heights[1](10, 10)) << "the upscaling adds detail";

    region.blendCells = 6;
    const NestedTerrain blended(Hills(), Base(), {region});
    const auto edged = blended.EvaluateAll(options);
    for (std::size_t col = 1; col < 41; col += 2) {
        const double between = 0.5 * (static_cast<double>(edged[0](col0 + col / 2, row0)) +
                                      static_cast<double>(edged[0](col0 + col / 2 + 1, row0)));
        EXPECT_NEAR(edged[1](col, 0), between, 1e-4) << "linear between the cells above";
    }
    for (std::size_t row = 6; row < 35; ++row) {
        for (std::size_t col = 6; col < 35; ++col) {
            ASSERT_EQ(edged[1](col, row), heights[1](col, row)) << "untouched inside the band";
        }
    }
    EXPECT_THROW(NestedTerrain(Hills(), Base(), {[] {
                                   NestedRegion bad = Region();
                                   bad.upscaling = {nullptr};
                                   return bad;
                               }()}),
                 std::invalid_argument);
}

TEST(NestedTerrain, MergesTheLevelsIntoOneMeshCutUnderTheFinerOnes) {
    const NestedTerrain nested(Hills(), Base(), {Region()});
    const auto heights = nested.EvaluateAll();
    const std::size_t baseSquares = 64u * 64u;
    const std::size_t levelSquares = 40u * 40u;
    const auto whole = nested.Mesh(heights, {0});
    EXPECT_EQ(whole.triangles.size(), 2u * baseSquares) << "nothing cut without its children";
    const auto all = nested.Mesh(heights, {0, 1, 2});
    EXPECT_EQ(all.triangles.size(),
              2u * (baseSquares - 20u * 20u) + 2u * (levelSquares - 20u * 20u) + 2u * levelSquares);
    EXPECT_EQ(all.positions.size(), 65u * 65u + 2u * 41u * 41u);
    const auto skirted = nested.Mesh(heights, {0, 1}, 1.0);
    EXPECT_GT(skirted.triangles.size(), nested.Mesh(heights, {0, 1}).triangles.size());
    EXPECT_EQ(nested.MeshOptions(0, 2.0).cutouts.size(), 1u);
    EXPECT_EQ(nested.MeshOptions(2).cutouts.size(), 0u);
    EXPECT_THROW(nested.Mesh({}, {0}), std::invalid_argument);
    EXPECT_THROW(nested.Mesh(heights, {3}), std::invalid_argument);
    auto swapped = heights;
    std::swap(swapped[1], swapped[2]);
    EXPECT_THROW(nested.Mesh(swapped, {1}), std::invalid_argument);
}
