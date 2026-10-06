#include "terraingen/pipeline/tiling.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <unordered_set>

using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::TileKey;
using terraingen::TileScheme;

TEST(TileScheme, HalvesTilesAndSpacingWithEachLevel) {
    const TileScheme scheme{{-100.0, 200.0}, 512.0, 128};
    EXPECT_EQ(scheme.TileSize(0), 512.0);
    EXPECT_EQ(scheme.TileSize(3), 64.0);
    EXPECT_EQ(scheme.Spacing(0), 4.0);
    EXPECT_EQ(scheme.Spacing(2), 1.0);
    EXPECT_EQ(scheme.LevelFor(4.0), 0);
    EXPECT_EQ(scheme.LevelFor(3.9), 1);
    EXPECT_EQ(scheme.LevelFor(1.0), 2);
    EXPECT_EQ(scheme.LevelFor(100.0), 0);
    EXPECT_THROW(scheme.LevelFor(0.0), std::invalid_argument);
    EXPECT_THROW(scheme.TileSize(-1), std::invalid_argument);
}

TEST(TileScheme, LocatesTilesAndTheirGrids) {
    const TileScheme scheme{{-100.0, 200.0}, 512.0, 128};
    EXPECT_EQ(scheme.TileAt(-100.0, 200.0, 0), (TileKey{0, 0, 0}));
    EXPECT_EQ(scheme.TileAt(-100.1, 200.1, 0), (TileKey{0, -1, -1}));
    EXPECT_EQ(scheme.TileAt(30.0, 100.0, 2), (TileKey{2, 1, 0}));
    const RasterGrid grid = scheme.Grid({2, 1, 0});
    EXPECT_EQ(grid.width, 128u);
    EXPECT_EQ(grid.transform, GeoTransform::NorthUp(28.0, 200.0, 1.0));
    const auto keys = scheme.TilesCovering({-90.0, -100.0, 300.0, 190.0}, 2);
    ASSERT_EQ(keys.size(), 12u) << "4 tiles of 128 m west to east, 3 north to south";
    EXPECT_EQ(keys.front(), (TileKey{2, 0, 0}));
    EXPECT_EQ(keys[1], (TileKey{2, 1, 0}));
    EXPECT_EQ(keys.back(), (TileKey{2, 3, 2}));
}

TEST(TileScheme, LinksParentsAndChildren) {
    for (const TileKey key : {TileKey{3, 5, -7}, TileKey{1, -1, 0}, TileKey{4, 0, 0}}) {
        const auto children = TileScheme::Children(key);
        std::unordered_set<std::size_t> seen;
        for (const TileKey& child : children) {
            EXPECT_EQ(child.level, key.level + 1);
            EXPECT_EQ(TileScheme::Parent(child), key);
            seen.insert(terraingen::TileKeyHash{}(child));
        }
        EXPECT_EQ(seen.size(), 4u);
    }
    EXPECT_THROW(TileScheme::Parent({0, 0, 0}), std::invalid_argument);
}

TEST(TileScheme, CoversAGridWithTilesOnItsLattice) {
    const RasterGrid grid{300, 200, GeoTransform::NorthUp(1000.0, 5000.0, 0.5)};
    const TileScheme scheme = TileScheme::Covering(grid, 64);
    EXPECT_EQ(scheme.origin.x, 1000.0);
    EXPECT_EQ(scheme.origin.y, 5000.0);
    EXPECT_EQ(scheme.rootSize, 256.0);
    EXPECT_EQ(scheme.Spacing(3), 0.5);
    EXPECT_GE(scheme.rootSize, 150.0);
    EXPECT_THROW(TileScheme::Covering(RasterGrid{}, 64), std::invalid_argument);
    EXPECT_THROW(TileScheme::Covering({4, 4, GeoTransform{0.0, 1.0, 0.2, 0.0, 0.0, -1.0}}, 64),
                 std::invalid_argument);
}

TEST(TileScheme, ValidatesItsParameters) {
    EXPECT_NO_THROW((TileScheme{{0.0, 0.0}, 10.0, 8}.Validate()));
    EXPECT_THROW((TileScheme{{0.0, 0.0}, 0.0, 8}.Validate()), std::invalid_argument);
    EXPECT_THROW((TileScheme{{0.0, 0.0}, 10.0, 0}.Validate()), std::invalid_argument);
    EXPECT_THROW((TileScheme{{std::numeric_limits<double>::infinity(), 0.0}, 10.0, 8}.Validate()),
                 std::invalid_argument);
}
