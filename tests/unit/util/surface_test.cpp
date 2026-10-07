#include "terraingen/util/cells.h"
#include "terraingen/util/surface.h"

#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::Bounds2d;
using terraingen::CellIndex;
using terraingen::CubeSphereCells;
using terraingen::RandomStream;
using terraingen::Surface;
using terraingen::Vec2d;
using terraingen::Vec3d;

namespace {

constexpr double kMoon = 1737400.0;

}  // namespace

TEST(Surface, PlaneOffsetsAreDifferencesExactly) {
    const Surface plane;
    EXPECT_FALSE(plane.IsSphere());
    EXPECT_EQ(plane, Surface::Plane());
    EXPECT_EQ(plane.UnitLength(), 1.0);
    EXPECT_EQ(plane.Radius(), 0.0);
    const Vec2d centre{1000.25, -3.5};
    const Vec2d point{1003.0, 7.125};
    const Vec2d offset = plane.Offset(centre, point);
    EXPECT_EQ(offset.x, point.x - centre.x);
    EXPECT_EQ(offset.y, point.y - centre.y);
    const Vec2d back = plane.Locate(centre, offset);
    EXPECT_EQ(back.x, point.x);
    EXPECT_EQ(back.y, point.y);
    EXPECT_EQ(plane.Distance({0.0, 0.0}, {3.0, 4.0}), 5.0);
    EXPECT_EQ(plane.Position({2.0, 3.0}), (Vec3d{2.0, 3.0, 0.0}));
    const Vec2d flat = plane.PointOf({2.0, 3.0, 9.0});
    EXPECT_EQ(flat.x, 2.0);
    EXPECT_EQ(flat.y, 3.0);
    const Bounds2d grown = plane.Expand({0.0, 0.0, 1.0, 1.0}, 2.0);
    EXPECT_EQ(grown.minX, -2.0);
    EXPECT_EQ(grown.maxY, 3.0);
}

TEST(Surface, SphereOffsetsAreAzimuthalEquidistant) {
    const Surface moon = Surface::Sphere(kMoon);
    EXPECT_TRUE(moon.IsSphere());
    EXPECT_NEAR(moon.UnitLength(), kMoon * std::numbers::pi / 180.0, 1e-9);
    const double quarter = kMoon * std::numbers::pi / 2.0;

    const Vec2d east = moon.Offset({0.0, 0.0}, {90.0, 0.0});
    EXPECT_NEAR(east.x, quarter, 1e-6);
    EXPECT_NEAR(east.y, 0.0, 1e-6);
    const Vec2d north = moon.Offset({0.0, 0.0}, {0.0, 1.0});
    EXPECT_NEAR(north.x, 0.0, 1e-6);
    EXPECT_NEAR(north.y, moon.UnitLength(), 1e-6);
    const Vec2d pole = moon.Offset({30.0, 45.0}, {123.0, 90.0});
    EXPECT_NEAR(pole.x, 0.0, 1e-6);
    EXPECT_NEAR(pole.y, quarter / 2.0, 1e-6);
    const Vec2d antipode = moon.Offset({10.0, 0.0}, {-170.0, 0.0});
    EXPECT_NEAR(std::hypot(antipode.x, antipode.y), 2.0 * quarter, 1e-3);
    EXPECT_EQ(moon.Offset({5.0, 5.0}, {5.0, 5.0}).x, 0.0);

    RandomStream random(3, 4);
    for (int i = 0; i < 200; ++i) {
        const Vec2d centre{360.0 * random.NextDouble() - 180.0, 178.0 * random.NextDouble() - 89.0};
        const Vec2d offset{5e5 * (random.NextDouble() - 0.5), 5e5 * (random.NextDouble() - 0.5)};
        const Vec2d point = moon.Locate(centre, offset);
        EXPECT_GT(point.x, -180.0);
        EXPECT_LE(point.x, 180.0);
        const Vec2d again = moon.Offset(centre, point);
        EXPECT_NEAR(again.x, offset.x, 1e-6);
        EXPECT_NEAR(again.y, offset.y, 1e-6);
        EXPECT_NEAR(moon.Distance(centre, point), std::hypot(offset.x, offset.y), 1e-6);
    }
    EXPECT_EQ(moon.Locate({12.0, -3.0}, {0.0, 0.0}).x, 12.0);
    EXPECT_NEAR(moon.Offset({179.9, 0.0}, {-179.9, 0.0}).x, 0.2 * moon.UnitLength(), 1e-6);
}

TEST(Surface, SpherePositionsAndPoints) {
    const Surface moon = Surface::Sphere(kMoon);
    const Vec3d p = moon.Position({90.0, 0.0});
    EXPECT_NEAR(p.x, 0.0, 1e-6);
    EXPECT_NEAR(p.y, kMoon, 1e-6);
    EXPECT_NEAR(moon.Position({0.0, 90.0}).z, kMoon, 1e-6);
    const Vec2d back = moon.PointOf({-1.0, 0.0, 0.0});
    EXPECT_EQ(back.x, 180.0);
    EXPECT_EQ(back.y, 0.0);
    const Vec2d south = moon.PointOf({0.0, 0.0, -3.0});
    EXPECT_EQ(south.y, -90.0);
    EXPECT_THROW(Surface::Sphere(0.0), std::invalid_argument);
    EXPECT_THROW(Surface::Sphere(NAN), std::invalid_argument);
}

TEST(Surface, SphereExpandCoversEveryPointWithinTheDistance) {
    const Surface moon = Surface::Sphere(kMoon);
    const Bounds2d region{10.0, 40.0, 12.0, 60.0};
    const double metres = 50000.0;
    const Bounds2d grown = moon.Expand(region, metres);
    RandomStream random(5, 6);
    for (int i = 0; i < 2000; ++i) {
        const Vec2d inside{region.minX + 2.0 * random.NextDouble(),
                           region.minY + 20.0 * random.NextDouble()};
        const double azimuth = 2.0 * std::numbers::pi * random.NextDouble();
        const Vec2d reached =
            moon.Locate(inside, {metres * std::sin(azimuth), metres * std::cos(azimuth)});
        EXPECT_GE(reached.x, grown.minX - 1e-9);
        EXPECT_LE(reached.x, grown.maxX + 1e-9);
        EXPECT_GE(reached.y, grown.minY - 1e-9);
        EXPECT_LE(reached.y, grown.maxY + 1e-9);
    }
    const Bounds2d polar = moon.Expand({0.0, 85.0, 1.0, 88.0}, 200000.0);
    EXPECT_EQ(polar.minX, -180.0);
    EXPECT_EQ(polar.maxX, 180.0);
    EXPECT_EQ(polar.maxY, 90.0);
    const Bounds2d wide = moon.Expand({-170.0, 0.0, 170.0, 1.0}, 400000.0);
    EXPECT_EQ(wide.minX, -180.0);
    const Bounds2d huge = moon.Expand({0.0, 0.0, 1.0, 1.0}, 3.0 * kMoon);
    EXPECT_EQ(huge.minX, -180.0);
    EXPECT_EQ(huge.minY, -90.0);
}

TEST(CubeSphereCells, CellsTileTheSphereWithExactAreas) {
    const CubeSphereCells cells(kMoon);
    const double size = 300000.0;
    const std::int32_t n = cells.CellsPerEdge(size);
    EXPECT_EQ(n, static_cast<std::int32_t>(std::ceil(kMoon * std::numbers::pi / 2.0 / size)));
    double total = 0.0;
    double smallest = 1e300;
    double largest = 0.0;
    for (std::int32_t i = 0; i < 6 * n; ++i) {
        for (std::int32_t j = 0; j < n; ++j) {
            const double area = cells.CellArea({i, j}, size);
            total += area;
            smallest = std::min(smallest, area);
            largest = std::max(largest, area);
        }
    }
    EXPECT_NEAR(total / (4.0 * std::numbers::pi * kMoon * kMoon), 1.0, 1e-12);
    EXPECT_LT(largest / smallest, 1.5);
    EXPECT_LE(largest, size * size);
    EXPECT_EQ(cells.Radius(), kMoon);
    EXPECT_EQ(cells.CellsPerEdge(1e12), 1);
    EXPECT_THROW(cells.CellsPerEdge(0.0), std::invalid_argument);
    EXPECT_THROW(cells.CellsPerEdge(1e-9), std::invalid_argument);
    EXPECT_THROW(CubeSphereCells(-1.0), std::invalid_argument);
}

TEST(CubeSphereCells, SamplesAreUniformOverEachCell) {
    const CubeSphereCells cells(kMoon);
    const double size = kMoon * std::numbers::pi / 6.0 * 1.000001;
    const std::int32_t n = cells.CellsPerEdge(size);
    const CellIndex corner{4 * n + n - 1, n - 1};
    RandomStream random(7, 8);
    int inLowerHalf = 0;
    const int draws = 20000;
    const auto lowerHalf = [&](const Vec2d& p) {
        const CellIndex fine = cells.CellOf(p.x, p.y, size / 2.0);
        return fine.j % 2 == 0;
    };
    for (int k = 0; k < draws; ++k) {
        const Vec2d p = cells.SamplePoint(random.NextDouble(), random.NextDouble(), corner, size);
        EXPECT_EQ(cells.CellOf(p.x, p.y, size), corner);
        inLowerHalf += lowerHalf(p) ? 1 : 0;
    }
    const std::int32_t m = cells.CellsPerEdge(size / 2.0);
    ASSERT_EQ(m, 2 * n);
    double lower = 0.0;
    double whole = 0.0;
    for (std::int32_t a = 2 * (n - 1); a < 2 * n; ++a) {
        for (std::int32_t b = 2 * (n - 1); b < 2 * n; ++b) {
            const double area = cells.CellArea({4 * m + a, b}, size / 2.0);
            whole += area;
            lower += b % 2 == 0 ? area : 0.0;
        }
    }
    EXPECT_NEAR(whole, cells.CellArea(corner, size), 1e-3 * whole);
    const double expected = lower / whole;
    const double sigma = std::sqrt(expected * (1.0 - expected) / draws);
    EXPECT_NEAR(static_cast<double>(inLowerHalf) / draws, expected, 4.0 * sigma);
    EXPECT_NE(expected, 0.5);
}

TEST(CubeSphereCells, CoveringFindsEveryCellReachingTheRegion) {
    const CubeSphereCells cells(kMoon);
    const double size = 40000.0;
    for (const Bounds2d& region :
         {Bounds2d{44.0, 30.0, 47.0, 36.0}, Bounds2d{178.0, -2.0, 182.0, 2.0},
          Bounds2d{-10.0, 86.0, 10.0, 90.0}, Bounds2d{3.0, 3.0, 3.0, 3.0}}) {
        const std::vector<CellIndex> covering = cells.CellsCovering(region, size);
        ASSERT_FALSE(covering.empty());
        for (std::size_t k = 1; k < covering.size(); ++k) {
            const CellIndex& a = covering[k - 1];
            const CellIndex& b = covering[k];
            EXPECT_TRUE(a.i < b.i || (a.i == b.i && a.j < b.j));
        }
        const std::set<std::pair<int, int>> found = [&] {
            std::set<std::pair<int, int>> set;
            for (const CellIndex& c : covering) {
                set.insert({c.i, c.j});
            }
            return set;
        }();
        RandomStream random(9, 10);
        for (int k = 0; k < 3000; ++k) {
            const double lon = region.minX + (region.maxX - region.minX) * random.NextDouble();
            const double lat = region.minY + (region.maxY - region.minY) * random.NextDouble();
            const CellIndex c = cells.CellOf(lon, lat, size);
            EXPECT_TRUE(found.count({c.i, c.j})) << lon << " " << lat;
        }
    }
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, -1.0, 1.0}, size), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, NAN, 1.0}, size), std::invalid_argument);
}

TEST(CubeSphereCells, TwoRingsOfNeighboursHoldEveryPointWithinACellAcrossFacesAndCorners) {
    const CubeSphereCells cells(kMoon);
    const Surface moon = Surface::Sphere(kMoon);
    const double size = 50000.0;
    RandomStream random(11, 12);
    const double corner = std::atan(1.0 / std::sqrt(2.0)) * 180.0 / std::numbers::pi;
    std::vector<Vec2d> centres{{45.0, corner}, {-135.0, -corner}, {45.0, 0.0}, {0.0, 45.0}};
    for (int k = 0; k < 300; ++k) {
        centres.push_back(
            {360.0 * random.NextDouble() - 180.0, 180.0 * random.NextDouble() - 90.0});
    }
    for (const Vec2d& centre : centres) {
        const CellIndex home = cells.CellOf(centre.x, centre.y, size);
        const std::vector<CellIndex> around = cells.Neighbours(home, size, 2);
        EXPECT_TRUE(std::find(around.begin(), around.end(), home) != around.end());
        for (int k = 0; k < 60; ++k) {
            const double distance = 0.91 * size * std::sqrt(random.NextDouble());
            const double azimuth = 2.0 * std::numbers::pi * random.NextDouble();
            const Vec2d there =
                moon.Locate(centre, {distance * std::sin(azimuth), distance * std::cos(azimuth)});
            const CellIndex cell = cells.CellOf(there.x, there.y, size);
            EXPECT_TRUE(std::find(around.begin(), around.end(), cell) != around.end())
                << centre.x << " " << centre.y;
        }
    }
    const std::vector<CellIndex> inner = cells.Neighbours({5, 5}, size, 1);
    EXPECT_EQ(inner.size(), 9u);
}
