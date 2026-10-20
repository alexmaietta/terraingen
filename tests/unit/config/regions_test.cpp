#include "terraingen/config/regions.h"

#include "terraingen/config/build.h"
#include "terraingen/config/json.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using terraingen::GridRegionSpec;
using terraingen::RegionSpec;
using terraingen::TerrainConfig;
using terraingen::config::LayOutRegions;
using terraingen::config::ResolveOutputs;

namespace {

/// 65 x 65 cells of 2 m centred on the origin.
terraingen::RasterGrid Base() {
    return {65, 65, terraingen::GeoTransform::NorthUp(-65.0, 65.0, 2.0)};
}

/// A region of `count` levels 40 m across about (`x`, `y`).
RegionSpec Region(const std::string& name, double x, double y, std::size_t count = 2) {
    RegionSpec region;
    region.name = name;
    region.centreX = x;
    region.centreY = y;
    region.repeat = terraingen::RepeatedLevelsSpec{count, 2, 2.0, 40.0};
    return region;
}

}  // namespace

TEST(RegionLevels, TakeTheListOrTheRepeat) {
    RegionSpec listed;
    listed.levels = {{1.0, 30.0}, {0.25, 10.0}};
    EXPECT_EQ(terraingen::config::RegionLevels(listed, 2.0),
              (std::vector<terraingen::NestedLevel>{{1.0, 30.0}, {0.25, 10.0}}));
    EXPECT_EQ(terraingen::config::RegionLevelCount(listed), 2u);
    const RegionSpec repeated = Region("a", 0.0, 0.0, 3);
    EXPECT_EQ(terraingen::config::RegionLevels(repeated, 2.0).back(),
              (terraingen::NestedLevel{0.25, 10.0}));
    EXPECT_EQ(terraingen::config::RegionLevelCount(repeated), 3u);
}

TEST(LayOutRegions, GroupsRegionsByBaseAndIndexesTheirLevels) {
    RegionSpec apart = Region("apart", 0.0, 0.0, 1);
    apart.base = GridRegionSpec{33, 33, 4.0, 500.0, 500.0};
    apart.centreX = 500.0;
    apart.centreY = 500.0;
    const auto layout = LayOutRegions(
        {Region("west", -30.0, 20.0), apart, Region("east", 30.0, -20.0, 1)}, Base(), {});
    ASSERT_EQ(layout.groups.size(), 2u);
    EXPECT_EQ(layout.groups[0].base, Base());
    EXPECT_EQ(layout.groups[0].regions, (std::vector<std::size_t>{0u, 2u}));
    EXPECT_EQ(layout.groups[1].regions, std::vector<std::size_t>{1u});
    EXPECT_EQ(layout.groups[0].grids.size(), 1u + 2u + 1u);
    EXPECT_EQ(layout.group, (std::vector<std::size_t>{0u, 1u, 0u}));
    EXPECT_EQ(layout.member, (std::vector<std::size_t>{0u, 0u, 1u}));
    EXPECT_EQ(layout.Find("east"), 2u);
    EXPECT_FALSE(layout.Find("north").has_value());
    EXPECT_EQ(layout.GridIndex(0, 0), 0u);
    EXPECT_EQ(layout.GridIndex(0, 2), 2u);
    EXPECT_EQ(layout.GridIndex(2, 1), 3u);
    EXPECT_THROW(layout.GridIndex(2, 2), std::out_of_range);

    RegionSpec bad = Region("bad", 0.0, 0.0);
    bad.repeat->factor = 1;
    try {
        LayOutRegions({Region("ok", -40.0, 40.0, 1), bad}, Base(), {});
        ADD_FAILURE() << "a factor of 1";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(), "regions[1]");
    }
}

TEST(ResolveOutputs, ExpandsEveryLevelAndCutsLevelMeshesUnderTheRegionsFinerOnes) {
    TerrainConfig config;
    config.regions = {Region("west", -30.0, 20.0), Region("east", 30.0, -20.0, 1)};
    terraingen::HeightmapOutput levels;
    levels.path = "{region}/{level}.tif";
    levels.nested = terraingen::NestedLevelRef{"west", std::nullopt};
    terraingen::TerrainMeshOutput base;
    base.path = "base.glb";
    base.nested = terraingen::NestedLevelRef{"east", 0};
    terraingen::HeightmapOutput plain;
    plain.path = "plain.tif";
    config.outputs = {levels, base, plain,
                      terraingen::NestedMeshOutput{"{region}_{level}.glb", {"west", "east"}, 0.0},
                      terraingen::ManifestOutput{"m.json", {}}};
    const auto layout = LayOutRegions(config.regions, Base(), {});
    const auto resolved = ResolveOutputs(config, Base(), {}, layout);
    ASSERT_EQ(resolved.size(), 3u + 1u + 1u + 1u + 1u);
    const auto path = [&](std::size_t i) {
        return std::visit([](const auto& o) { return o.path; }, resolved[i].spec);
    };
    EXPECT_EQ(path(0), "west/0.tif");
    EXPECT_EQ(path(2), "west/2.tif");
    EXPECT_EQ(resolved[0].where, "outputs[0]");
    EXPECT_FALSE(resolved[0].region.has_value()) << "the base comes from the terrain";
    EXPECT_EQ(resolved[2].region, 0u);
    EXPECT_EQ(resolved[2].grids, std::vector<std::size_t>{2u});
    EXPECT_EQ(resolved[2].grid, layout.groups[0].grids[2].grid);

    const auto& mesh = std::get<terraingen::TerrainMeshOutput>(resolved[3].spec);
    ASSERT_EQ(mesh.cutouts.size(), 1u) << "only under east's level 1";
    const auto east = layout.groups[0].grids[3].grid;
    EXPECT_NEAR(mesh.cutouts[0].minX, east.transform.PixelToWorld(0.5, 0.5).x, 1e-5);
    EXPECT_EQ(mesh.siteX, 0.0);
    EXPECT_EQ(resolved[4].grid, Base()) << "a plain output covers the base";
    EXPECT_FALSE(resolved[4].group.has_value());

    const auto& nested = resolved[5];
    EXPECT_FALSE(nested.grid.has_value());
    EXPECT_EQ(nested.grids, (std::vector<std::size_t>{0u, 1u, 2u, 3u}));
    const auto& output = std::get<terraingen::NestedMeshOutput>(nested.spec);
    EXPECT_TRUE(terraingen::config::SplitsNestedMesh(output));
    EXPECT_EQ(terraingen::config::NestedMeshFile(output, layout, 0, 0), "west_0.glb");
    EXPECT_EQ(terraingen::config::NestedMeshFile(output, layout, 0, 3), "east_1.glb");
    EXPECT_TRUE(std::holds_alternative<terraingen::ManifestOutput>(resolved[6].spec));
}
