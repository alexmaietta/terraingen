#include "terraingen/util/cells.h"

#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::Bounds2d;
using terraingen::CellIndex;
using terraingen::PlanarCells;
using terraingen::RandomStream;

TEST(Bounds2d, ContainsIsHalfOpenAndExpandsOnEverySide) {
    const Bounds2d box{0.0, 0.0, 10.0, 5.0};
    EXPECT_TRUE(box.Contains(0.0, 0.0));
    EXPECT_TRUE(box.Contains(9.999, 4.999));
    EXPECT_FALSE(box.Contains(10.0, 1.0));
    EXPECT_FALSE(box.Contains(1.0, 5.0));
    EXPECT_FALSE(box.Contains(-0.001, 1.0));
    const Bounds2d grown = box.Expanded(2.0);
    EXPECT_EQ(grown.minX, -2.0);
    EXPECT_EQ(grown.minY, -2.0);
    EXPECT_EQ(grown.maxX, 12.0);
    EXPECT_EQ(grown.maxY, 7.0);
}

TEST(PlanarCells, CoversARegionInRowMajorOrderIncludingNegativeCells) {
    const PlanarCells cells;
    const std::vector<CellIndex> covering = cells.CellsCovering({-5.0, 0.0, 10.0, 10.0}, 5.0);
    const std::vector<CellIndex> expected{{-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    EXPECT_EQ(covering, expected);
    EXPECT_EQ(cells.CellsCovering({0.5, 0.5, 1.0, 1.0}, 5.0), (std::vector<CellIndex>{{0, 0}}));
    EXPECT_EQ(cells.CellsCovering({-0.5, 0.5, 0.0, 1.0}, 5.0), (std::vector<CellIndex>{{-1, 0}}));
    EXPECT_EQ(cells.CellArea({3, -2}, 5.0), 25.0);
}

TEST(PlanarCells, SamplesPointsInsideTheCell) {
    const PlanarCells cells;
    RandomStream stream(3u, 4u);
    for (int n = 0; n < 100; ++n) {
        const double u = stream.NextDouble();
        const double v = stream.NextDouble();
        const auto point = cells.SamplePoint(u, v, {-3, 7}, 2.0);
        EXPECT_DOUBLE_EQ(point.x, (-3 + u) * 2.0);
        EXPECT_DOUBLE_EQ(point.y, (7 + v) * 2.0);
        EXPECT_GE(point.x, -6.0);
        EXPECT_LT(point.x, -4.0);
    }
}

TEST(SurfaceCells, PlanarCellsRejectEmptyRegionsWhileCubeSphereCellsCoverPoints) {
    const PlanarCells planar;
    const terraingen::CubeSphereCells sphere(1737400.0);
    EXPECT_THROW(planar.CellsCovering({10.0, 20.0, 10.0, 20.0}, 5e4), std::invalid_argument);
    const std::vector<CellIndex> point = sphere.CellsCovering({10.0, 20.0, 10.0, 20.0}, 5e4);
    EXPECT_TRUE(std::binary_search(point.begin(), point.end(), sphere.CellAt(10.0, 20.0, 5e4)));
    for (const terraingen::SurfaceCells* cells :
         {static_cast<const terraingen::SurfaceCells*>(&planar),
          static_cast<const terraingen::SurfaceCells*>(&sphere)}) {
        EXPECT_THROW(cells->CellsCovering({11.0, 20.0, 10.0, 21.0}, 5e4), std::invalid_argument);
        EXPECT_FALSE(cells->CellsCovering({10.0, 20.0, 11.0, 21.0}, 5e4).empty());
    }
}

TEST(MakeCells, PicksPlanarOrCubeSphereCellsByTheSurface) {
    const auto plane = terraingen::MakeCells(terraingen::Surface::Plane());
    EXPECT_NE(dynamic_cast<const PlanarCells*>(plane.get()), nullptr);
    const auto body = terraingen::MakeCells(terraingen::Surface::Ellipsoid(3.0, 2.0, 1.0));
    const auto* cells = dynamic_cast<const terraingen::CubeSphereCells*>(body.get());
    ASSERT_NE(cells, nullptr);
    EXPECT_EQ(cells->Radius(), 1.0);
}

TEST(PlanarCells, RejectsInvalidRegionsAndCellSizes) {
    const PlanarCells cells;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, 1.0, 1.0}, 0.0), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, 1.0, 1.0}, kInf), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, 0.0, 1.0}, 1.0), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 2.0, 1.0, 1.0}, 1.0), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({-kInf, 0.0, 1.0, 1.0}, 1.0), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, 1e12, 1.0}, 1e-3), std::invalid_argument);
}
