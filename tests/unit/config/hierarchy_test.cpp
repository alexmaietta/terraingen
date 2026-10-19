#include "terraingen/config/hierarchy.h"

#include "terraingen/config/build.h"
#include "terraingen/config/json.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

using terraingen::ConfigError;
using terraingen::GridRegionSpec;
using terraingen::HierarchyProduct;
using terraingen::HierarchyRegionSpec;
using terraingen::HierarchySpec;
using terraingen::RasterGrid;
using terraingen::config::HierarchyGrids;

namespace {

/// A hierarchy over 65 x 65 cells of 2 m with one region of two levels.
HierarchySpec TwoLevels() {
    HierarchySpec hierarchy;
    hierarchy.base = GridRegionSpec{65, 65, 2.0, 0.0, 0.0};
    HierarchyRegionSpec region;
    region.centreX = 13.3;
    region.centreY = -7.9;
    region.size = 40.0;
    region.levels = 2;
    hierarchy.regions = {region};
    return hierarchy;
}

/// The cell centres at the corners of `grid`: first, then last.
std::pair<terraingen::Vec2d, terraingen::Vec2d> Corners(const GridRegionSpec& grid) {
    const RasterGrid cells = terraingen::config::RegionGrid(grid, {});
    return {cells.transform.PixelToWorld(0.5, 0.5),
            cells.transform.PixelToWorld(static_cast<double>(cells.width) - 0.5,
                                         static_cast<double>(cells.height) - 0.5)};
}

/// The path of the `ConfigError` `run` throws, or "none".
template <typename Run> std::string ErrorPath(Run run) {
    try {
        run();
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "none";
}

}  // namespace

TEST(HierarchyGrids, NestEachLevelOnItsParentsCellCentres) {
    const auto grids = HierarchyGrids(TwoLevels(), std::nullopt, {});
    ASSERT_EQ(grids.size(), 3u);
    EXPECT_EQ(grids[0].name, "region_0_level_0");
    EXPECT_EQ(grids[1].name, "region_0_level_1");
    EXPECT_EQ(grids[2].name, "region_0_level_2");
    EXPECT_FALSE(grids[0].parent.has_value());
    EXPECT_EQ(grids[1].parent, 0u);
    EXPECT_EQ(grids[2].parent, 1u);
    EXPECT_EQ(grids[0].children, std::vector<std::size_t>{1u});
    EXPECT_EQ(grids[1].children, std::vector<std::size_t>{2u});
    EXPECT_TRUE(grids[2].children.empty());

    EXPECT_EQ(grids[1].grid, (GridRegionSpec{41, 41, 1.0, 14.0, -8.0})) << "snapped to a centre";
    EXPECT_EQ(grids[2].grid, (GridRegionSpec{41, 41, 0.5, 14.0, -8.0}));
    for (std::size_t level = 1; level < grids.size(); ++level) {
        const auto [first, last] = Corners(grids[level].grid);
        const auto [parentFirst, parentLast] = Corners(grids[level - 1].grid);
        const double step = grids[level - 1].grid.pixelSize;
        for (double offset : {first.x - parentFirst.x, last.x - parentFirst.x,
                              first.y - parentFirst.y, last.y - parentFirst.y}) {
            EXPECT_NEAR(offset / step, std::round(offset / step), 1e-9)
                << "edges on the parent's centres";
        }
        EXPECT_GT(first.x, parentFirst.x);
        EXPECT_LT(last.x, parentLast.x);
    }
}

TEST(HierarchyGrids, TakeTheBaseGridAndKeepCellCountsWithAShrinkOfOne) {
    HierarchySpec hierarchy = TwoLevels();
    hierarchy.base.reset();
    hierarchy.shrink = 1.0;
    hierarchy.regions[0].levels = 2;
    hierarchy.regions[0].size = 30.0;
    hierarchy.path = "site/{level}/r{region}";
    const RasterGrid base{65, 65, terraingen::GeoTransform::NorthUp(-65.0, 65.0, 2.0)};
    const auto grids = HierarchyGrids(hierarchy, base, {});
    ASSERT_EQ(grids.size(), 3u);
    EXPECT_EQ(grids[0].grid, (GridRegionSpec{65, 65, 2.0, 0.0, 0.0}));
    EXPECT_EQ(grids[1].grid.width, 33u) << "eight base cells each side of the centre";
    EXPECT_EQ(grids[2].grid.width, 61u) << "the same width at half the pixels";
    EXPECT_EQ(grids[2].name, "site/2/r0");
}

TEST(HierarchyGrids, RejectBadPathsMissingBasesOverlapsAndLevelsOutsideTheirParent) {
    HierarchySpec hierarchy = TwoLevels();
    hierarchy.path = "level_{level}";
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }), "hierarchy.path");

    hierarchy = TwoLevels();
    hierarchy.base.reset();
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }), "hierarchy.base");
    const RasterGrid oblong{10, 10, {0.0, 2.0, 0.0, 0.0, 0.0, -1.0}};
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, oblong, {}); }), "hierarchy.base");

    hierarchy = TwoLevels();
    hierarchy.regions.push_back(hierarchy.regions[0]);
    hierarchy.regions[1].centreX = -20.0;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }),
              "hierarchy.regions[1]");
    hierarchy.regions[1].centreX = -40.0;
    hierarchy.regions[1].centreY = 40.0;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }), "none")
        << "squares apart";
    EXPECT_EQ(HierarchyGrids(hierarchy, std::nullopt, {})[4].name, "region_1_level_2");

    hierarchy = TwoLevels();
    hierarchy.regions[0].centreX = 50.0;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }),
              "hierarchy.regions[0]");
    hierarchy = TwoLevels();
    hierarchy.shrink = 1.0;
    hierarchy.regions[0].size = 120.0;
    hierarchy.regions[0].centreX = 0.0;
    hierarchy.regions[0].centreY = 0.0;
    hierarchy.regions[0].levels = 1;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }), "none");
    hierarchy.regions[0].levels = 2;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }),
              "hierarchy.regions[0]")
        << "level 2 as wide as level 1";
    hierarchy = TwoLevels();
    hierarchy.factor = 4096;
    hierarchy.regions[0].levels = 1;
    EXPECT_EQ(ErrorPath([&] { HierarchyGrids(hierarchy, std::nullopt, {}); }),
              "hierarchy.regions[0]")
        << "too many cells";
}

TEST(HierarchyOutputs, WriteEachProductOverEachGridAndCutMeshesUnderFinerLevels) {
    HierarchySpec hierarchy = TwoLevels();
    hierarchy.products = {HierarchyProduct::Heightmap,   HierarchyProduct::TerrainMesh,
                          HierarchyProduct::Albedo,      HierarchyProduct::Materials,
                          HierarchyProduct::Photometry,  HierarchyProduct::CraterList,
                          HierarchyProduct::BoulderList, HierarchyProduct::BoulderModels};
    hierarchy.meshFormat = terraingen::HierarchyMeshFormat::Obj;
    hierarchy.skirt = 1.5;
    const auto grids = HierarchyGrids(hierarchy, std::nullopt, {});
    const auto outputs = terraingen::config::HierarchyOutputs(hierarchy, grids, {});
    ASSERT_EQ(outputs.size(), 3u * 8u);
    const auto path = [](const terraingen::OutputSpec& output) {
        return std::visit([](const auto& o) { return o.path; }, output);
    };
    const std::vector<std::string> expected{"region_0_level_1.tif",
                                            "region_0_level_1.obj",
                                            "region_0_level_1_albedo.tif",
                                            "region_0_level_1_materials.tif",
                                            "region_0_level_1_photometry.tif",
                                            "region_0_level_1_craters.csv",
                                            "region_0_level_1_boulders.csv",
                                            "region_0_level_1_boulders.obj"};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(path(outputs[8 + i]), expected[i]);
    }
    const auto& heightmap = std::get<terraingen::HeightmapOutput>(outputs[8]);
    EXPECT_EQ(heightmap.region, grids[1].grid);
    EXPECT_TRUE(std::holds_alternative<terraingen::BoulderModelsOutput>(outputs[15]));

    const auto& base = std::get<terraingen::TerrainMeshOutput>(outputs[1]);
    ASSERT_EQ(base.cutouts.size(), 1u);
    const auto [first, last] = Corners(grids[1].grid);
    EXPECT_NEAR(base.cutouts[0].minX, first.x, 1e-5);
    EXPECT_NEAR(base.cutouts[0].maxX, last.x, 1e-5);
    EXPECT_NEAR(base.cutouts[0].minY, last.y, 1e-5);
    EXPECT_NEAR(base.cutouts[0].maxY, first.y, 1e-5);
    EXPECT_LT(base.cutouts[0].minX, first.x) << "padded to hold the centres on its edge";
    EXPECT_EQ(base.skirt, 1.5);
    EXPECT_EQ(base.siteX, 0.0);
    const auto& finest = std::get<terraingen::TerrainMeshOutput>(outputs[17]);
    EXPECT_TRUE(finest.cutouts.empty());
    EXPECT_EQ(finest.siteY, 0.0) << "every level in the base's frame";

    terraingen::TerrainConfig config;
    config.outputs = {terraingen::HeightmapOutput{}};
    EXPECT_EQ(terraingen::config::ConfigOutputs(config, std::nullopt, {}).size(), 1u);
    config.hierarchy = hierarchy;
    EXPECT_EQ(terraingen::config::ConfigOutputs(config, std::nullopt, {}).size(), 25u);
}

TEST(HierarchyManifest, ListsEveryGridItsParentAndItsFiles) {
    HierarchySpec hierarchy = TwoLevels();
    hierarchy.products = {HierarchyProduct::Heightmap, HierarchyProduct::TerrainMesh};
    const auto grids = HierarchyGrids(hierarchy, std::nullopt, {});
    const auto manifest =
        nlohmann::json::parse(terraingen::config::HierarchyManifest(hierarchy, grids, {}));
    EXPECT_EQ(manifest["factor"], 2);
    EXPECT_EQ(manifest["shrink"], 2.0);
    ASSERT_EQ(manifest["grids"].size(), 3u);
    const auto& base = manifest["grids"][0];
    EXPECT_TRUE(base["parent"].is_null());
    EXPECT_EQ(base["width"], 65);
    EXPECT_EQ(base["bounds"], nlohmann::json::array({-65.0, -65.0, 65.0, 65.0}));
    const auto& finest = manifest["grids"][2];
    EXPECT_EQ(finest["name"], "region_0_level_2");
    EXPECT_EQ(finest["region"], 0);
    EXPECT_EQ(finest["level"], 2);
    EXPECT_EQ(finest["parent"], 1);
    EXPECT_EQ(finest["pixel_size"], 0.5);
    EXPECT_EQ(finest["centre_x"], 14.0);
    EXPECT_EQ(finest["files"]["heightmap"], "region_0_level_2.tif");
    EXPECT_EQ(finest["files"]["terrain_mesh"], "region_0_level_2.glb");
}
