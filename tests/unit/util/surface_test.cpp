// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/cells.h"
#include "terraingen/util/radial_shape.h"
#include "terraingen/util/surface.h"
#include "util/cube_faces.h"

#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST(Surface, PlaneOffsetsAreDifferencesExactly) {
    const Surface plane;
    EXPECT_FALSE(plane.IsBody());
    EXPECT_EQ(plane, Surface::Plane());
    EXPECT_EQ(plane.UnitLength(), 1.0);
    EXPECT_EQ(plane.Radius(), 0.0);
    const Vec2d center{1000.25, -3.5};
    const Vec2d point{1003.0, 7.125};
    const Vec2d offset = plane.Offset(center, point);
    EXPECT_EQ(offset.x, point.x - center.x);
    EXPECT_EQ(offset.y, point.y - center.y);
    const Vec2d back = plane.Locate(center, offset);
    EXPECT_EQ(back.x, point.x);
    EXPECT_EQ(back.y, point.y);
    EXPECT_EQ(plane.Distance({0.0, 0.0}, {3.0, 4.0}), 5.0);
    EXPECT_EQ(plane.Position({2.0, 3.0}), (Vec3d{2.0, 3.0, 0.0}));
    const Vec2d flat = plane.Coordinates({2.0, 3.0, 9.0});
    EXPECT_EQ(flat.x, 2.0);
    EXPECT_EQ(flat.y, 3.0);
    const Bounds2d grown = plane.Expand({0.0, 0.0, 1.0, 1.0}, 2.0);
    EXPECT_EQ(grown.minX, -2.0);
    EXPECT_EQ(grown.maxY, 3.0);
}

TEST(Surface, SphereOffsetsAreAzimuthalEquidistant) {
    const Surface moon = Surface::Sphere(kMoon);
    EXPECT_TRUE(moon.IsBody());
    EXPECT_NEAR(moon.UnitLength(), kMoon * terraingen::kPi / 180.0, 1e-9);
    const double quarter = kMoon * terraingen::kPi / 2.0;

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
        const Vec2d center{360.0 * random.NextDouble() - 180.0, 178.0 * random.NextDouble() - 89.0};
        const Vec2d offset{5e5 * (random.NextDouble() - 0.5), 5e5 * (random.NextDouble() - 0.5)};
        const Vec2d point = moon.Locate(center, offset);
        EXPECT_GT(point.x, -180.0);
        EXPECT_LE(point.x, 180.0);
        const Vec2d again = moon.Offset(center, point);
        EXPECT_NEAR(again.x, offset.x, 1e-6);
        EXPECT_NEAR(again.y, offset.y, 1e-6);
        EXPECT_NEAR(moon.Distance(center, point), std::hypot(offset.x, offset.y), 1e-6);
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
    const Vec2d back = moon.Coordinates({-1.0, 0.0, 0.0});
    EXPECT_EQ(back.x, 180.0);
    EXPECT_EQ(back.y, 0.0);
    const Vec2d south = moon.Coordinates({0.0, 0.0, -3.0});
    EXPECT_EQ(south.y, -90.0);
    EXPECT_THROW(Surface::Sphere(0.0), std::invalid_argument);
    EXPECT_THROW(Surface::Sphere(kNaN), std::invalid_argument);
}

TEST(Surface, SphereExpandCoversEveryPointWithinTheDistance) {
    const Surface moon = Surface::Sphere(kMoon);
    const Bounds2d region{10.0, 40.0, 12.0, 60.0};
    const double meters = 50000.0;
    const Bounds2d grown = moon.Expand(region, meters);
    RandomStream random(5, 6);
    for (int i = 0; i < 2000; ++i) {
        const Vec2d inside{region.minX + 2.0 * random.NextDouble(),
                           region.minY + 20.0 * random.NextDouble()};
        const double azimuth = 2.0 * terraingen::kPi * random.NextDouble();
        const Vec2d reached =
            moon.Locate(inside, {meters * std::sin(azimuth), meters * std::cos(azimuth)});
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

TEST(CubeFaces, FindTheNearestFaceAndEquiangularCoordinates) {
    using terraingen::detail::kCubeFaces;
    EXPECT_EQ(terraingen::detail::NearestFace(kCubeFaces, Vec3d{0.2, -3.0, 1.0}), 3u);
    EXPECT_EQ(terraingen::detail::NearestFace(kCubeFaces, Vec3d{1.0, 1.0, 0.0}), 0u)
        << "the first face on ties";
    EXPECT_DOUBLE_EQ(terraingen::detail::EquiangularCoordinate(-1.0, 8.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::detail::EquiangularCoordinate(0.0, 8.0), 4.0);
    EXPECT_DOUBLE_EQ(terraingen::detail::EquiangularCoordinate(1.0, 8.0), 8.0);
    EXPECT_DOUBLE_EQ(terraingen::detail::kQuarter, terraingen::kPi / 4.0);
}

TEST(CubeFaces, FaceCoordinatesGiveTheFaceAndItsEquiangularCoordinates) {
    using terraingen::detail::kCubeFaces;
    const terraingen::detail::FacePosition center =
        terraingen::detail::FaceCoordinates(kCubeFaces, Vec3d{0.0, -2.0, 0.0}, 8.0);
    EXPECT_EQ(center.face, 3u);
    EXPECT_DOUBLE_EQ(center.s, 4.0);
    EXPECT_DOUBLE_EQ(center.t, 4.0);
    const terraingen::detail::FacePosition edge =
        terraingen::detail::FaceCoordinates(kCubeFaces, Vec3d{1.0, 1.0, 0.0}, 8.0);
    EXPECT_EQ(edge.face, 0u);
    EXPECT_DOUBLE_EQ(edge.s, 8.0);
    EXPECT_DOUBLE_EQ(edge.t, 4.0);
}

TEST(CubeSphereCells, CellsTileTheSphereWithExactAreas) {
    const CubeSphereCells cells(kMoon);
    const double size = 300000.0;
    const std::int32_t n = cells.CellsPerEdge(size);
    EXPECT_EQ(n, static_cast<std::int32_t>(std::ceil(kMoon * terraingen::kPi / 2.0 / size)));
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
    EXPECT_NEAR(total / (4.0 * terraingen::kPi * kMoon * kMoon), 1.0, 1e-12);
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
    const double size = kMoon * terraingen::kPi / 6.0 * 1.000001;
    const std::int32_t n = cells.CellsPerEdge(size);
    const CellIndex corner{4 * n + n - 1, n - 1};
    RandomStream random(7, 8);
    int inLowerHalf = 0;
    const int draws = 20000;
    const auto lowerHalf = [&](const Vec2d& p) {
        const CellIndex fine = cells.CellAt(p.x, p.y, size / 2.0);
        return fine.j % 2 == 0;
    };
    for (int k = 0; k < draws; ++k) {
        const Vec2d p = cells.SamplePoint(random.NextDouble(), random.NextDouble(), corner, size);
        EXPECT_EQ(cells.CellAt(p.x, p.y, size), corner);
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
            const CellIndex c = cells.CellAt(lon, lat, size);
            EXPECT_TRUE(found.count({c.i, c.j})) << lon << " " << lat;
        }
    }
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, -1.0, 1.0}, size), std::invalid_argument);
    EXPECT_THROW(cells.CellsCovering({0.0, 0.0, kNaN, 1.0}, size), std::invalid_argument);
}

TEST(CubeSphereCells, CoveringKeepsCellsAtTheRegionEdgesAndDropsFarRingCells) {
    const CubeSphereCells cells(kMoon);
    const double size = 40000.0;
    const double corner = std::atan(1.0 / std::sqrt(2.0)) * 180.0 / terraingen::kPi;
    for (const Bounds2d& region :
         {Bounds2d{44.6, 34.9, 45.4, 35.3}, Bounds2d{44.9, -0.3, 45.1, 0.3},
          Bounds2d{44.8, corner - 0.2, 45.2, corner + 0.2}, Bounds2d{-0.4, 89.5, 0.4, 90.0},
          Bounds2d{179.8, 10.0, 180.4, 10.5}, Bounds2d{-30.0, -20.0, -29.0, -19.0}}) {
        const std::vector<CellIndex> covering = cells.CellsCovering(region, size);
        RandomStream random(11, 12);
        for (int k = 0; k < 4000; ++k) {
            const double t = random.NextDouble();
            const double lon = region.minX + (region.maxX - region.minX) * t;
            const double lat = region.minY + (region.maxY - region.minY) * t;
            for (const auto& [x, y] : {std::pair{lon, region.minY}, std::pair{lon, region.maxY},
                                       std::pair{region.minX, lat}, std::pair{region.maxX, lat}}) {
                const CellIndex c = cells.CellAt(x, y, size);
                EXPECT_TRUE(std::binary_search(covering.begin(), covering.end(), c))
                    << x << " " << y;
            }
        }
    }

    const CellIndex center = cells.CellAt(10.0, 10.0, size);
    const std::int32_t n = cells.CellsPerEdge(size);
    const double step = terraingen::kPi / 2.0 / n;
    const Vec3d direction = CubeSphereCells::Direction(
        center.i / n, -terraingen::kPi / 4.0 + (center.i % n + 0.5) * step,
        -terraingen::kPi / 4.0 + (center.j + 0.5) * step);
    const Vec2d point = Surface::Sphere(kMoon).Coordinates(direction);
    const std::vector<CellIndex> one =
        cells.CellsCovering(terraingen::PointBounds(point.x, point.y), size);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], center);
}

TEST(CubeSphereCells, SmallRegionsAreCoveredFromTheNeighborsOfTheirCenterCell) {
    const CubeSphereCells cells(kMoon);
    const Surface moon = Surface::Sphere(kMoon);
    const double size = 40000.0;
    const double corner = std::atan(1.0 / std::sqrt(2.0)) * 180.0 / terraingen::kPi;
    std::vector<Vec2d> centers{{45.0, corner}, {-135.0, -corner}, {45.0, 0.0},
                               {0.0, 45.0},    {135.0, 30.0},     {-45.0, -10.0}};
    RandomStream random(13, 14);
    for (int k = 0; k < 200; ++k) {
        centers.push_back(
            {360.0 * random.NextDouble() - 180.0, 150.0 * random.NextDouble() - 75.0});
    }
    for (const Vec2d& center : centers) {
        const double margin = 0.3 * size * random.NextDouble();
        const Bounds2d region = moon.Expand(terraingen::PointBounds(center.x, center.y), margin);
        const std::vector<CellIndex> covering = cells.CellsCovering(region, size);
        EXPECT_LE(covering.size(), 9u) << center.x << " " << center.y;
        EXPECT_TRUE(std::is_sorted(covering.begin(), covering.end()));
        for (int k = 0; k < 400; ++k) {
            const double lon = region.minX + (region.maxX - region.minX) * random.NextDouble();
            const double lat = region.minY + (region.maxY - region.minY) * random.NextDouble();
            const CellIndex c = cells.CellAt(lon, lat, size);
            EXPECT_TRUE(std::binary_search(covering.begin(), covering.end(), c))
                << center.x << " " << center.y << ": " << lon << " " << lat;
        }
    }
}

TEST(CubeSphereCells, TwoRingsOfNeighborsHoldEveryPointWithinACellAcrossFacesAndCorners) {
    const CubeSphereCells cells(kMoon);
    const Surface moon = Surface::Sphere(kMoon);
    const double size = 50000.0;
    RandomStream random(11, 12);
    const double corner = std::atan(1.0 / std::sqrt(2.0)) * 180.0 / terraingen::kPi;
    std::vector<Vec2d> centers{{45.0, corner}, {-135.0, -corner}, {45.0, 0.0}, {0.0, 45.0}};
    for (int k = 0; k < 300; ++k) {
        centers.push_back(
            {360.0 * random.NextDouble() - 180.0, 180.0 * random.NextDouble() - 90.0});
    }
    for (const Vec2d& center : centers) {
        const CellIndex home = cells.CellAt(center.x, center.y, size);
        const std::vector<CellIndex> around = cells.Neighbors(home, size, 2);
        EXPECT_TRUE(std::find(around.begin(), around.end(), home) != around.end());
        for (int k = 0; k < 60; ++k) {
            const double distance = 0.91 * size * std::sqrt(random.NextDouble());
            const double azimuth = 2.0 * terraingen::kPi * random.NextDouble();
            const Vec2d there =
                moon.Locate(center, {distance * std::sin(azimuth), distance * std::cos(azimuth)});
            const CellIndex cell = cells.CellAt(there.x, there.y, size);
            EXPECT_TRUE(std::find(around.begin(), around.end(), cell) != around.end())
                << center.x << " " << center.y;
        }
    }
    const std::vector<CellIndex> inner = cells.Neighbors({5, 5}, size, 1);
    EXPECT_EQ(inner.size(), 9u);
}

TEST(Surface, FramesAreOrthonormalWithUpAwayFromTheCenter) {
    const Surface moon = Surface::Sphere(kMoon);
    const terraingen::LocalFrame frame = moon.Frame({40.0, -30.0});
    const Vec3d& e = frame.east;
    const Vec3d& n = frame.north;
    const Vec3d& u = frame.up;
    const auto dot = [](const Vec3d& a, const Vec3d& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    EXPECT_NEAR(dot(e, e), 1.0, 1e-12);
    EXPECT_NEAR(dot(n, n), 1.0, 1e-12);
    EXPECT_NEAR(dot(e, n), 0.0, 1e-12);
    EXPECT_NEAR(dot(e, u), 0.0, 1e-12);
    const Vec3d p = moon.Position({40.0, -30.0});
    EXPECT_NEAR(dot(u, p), kMoon, 1e-6);
    const terraingen::LocalFrame plane = Surface().Frame({3.0, 4.0});
    EXPECT_EQ(plane.east, (Vec3d{1.0, 0.0, 0.0}));
    EXPECT_EQ(plane.north, (Vec3d{0.0, 1.0, 0.0}));
    EXPECT_EQ(plane.up, (Vec3d{0.0, 0.0, 1.0}));
}

TEST(Surface, BucketsAreCubesOfThePositionAndFlatOnAPlane) {
    EXPECT_EQ(Surface().Bucket({25.0, -5.0}, 10.0), (terraingen::BucketIndex{2, -1, 0}));
    const Surface moon = Surface::Sphere(kMoon);
    const terraingen::BucketIndex bucket = moon.Bucket({0.0, 90.0}, 1000.0);
    EXPECT_EQ(bucket.i, 0);
    EXPECT_EQ(bucket.k, static_cast<long long>(std::floor(kMoon / 1000.0)));
}

namespace {

/// An elongated body like Itokawa, in meters.
const Surface kEllipsoid = Surface::Ellipsoid(270.0, 147.0, 104.0);

double Dot3(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

}  // namespace

TEST(Surface, AnEllipsoidOfEqualRadiiIsTheSphere) {
    EXPECT_EQ(Surface::Ellipsoid(kMoon, kMoon, kMoon), Surface::Sphere(kMoon));
    EXPECT_EQ(Surface::Sphere(kMoon).Radii(), (Vec3d{kMoon, kMoon, kMoon}));
    EXPECT_EQ(Surface::Sphere(kMoon).RadiusAt({30.0, 20.0}), kMoon);
    EXPECT_EQ(Surface::Sphere(kMoon).AreaPerSteradian({30.0, 20.0}), kMoon * kMoon);
    EXPECT_EQ(Surface().RadiusAt({30.0, 20.0}), 0.0);
    EXPECT_EQ(Surface().AreaPerSteradian({30.0, 20.0}), 1.0);
    EXPECT_NE(kEllipsoid, Surface::Ellipsoid(270.0, 104.0, 147.0));
    EXPECT_TRUE(kEllipsoid.IsBody());
    EXPECT_NEAR(kEllipsoid.Radius(), std::cbrt(270.0 * 147.0 * 104.0), 1e-9);
    EXPECT_THROW(Surface::Ellipsoid(0.0, 1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(Surface::Ellipsoid(1.0, -1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(Surface::Ellipsoid(1.0, 1.0, kNaN), std::invalid_argument);
}

TEST(Surface, EllipsoidPositionsLieOnItInTheirDirection) {
    const Vec3d radii = kEllipsoid.Radii();
    for (int i = 0; i < 200; ++i) {
        const Vec2d point{static_cast<double>(i * 37 % 360) - 180.0,
                          static_cast<double>(i * 13 % 179) - 89.0};
        const Vec3d p = kEllipsoid.Position(point);
        EXPECT_NEAR(p.x * p.x / (radii.x * radii.x) + p.y * p.y / (radii.y * radii.y) +
                        p.z * p.z / (radii.z * radii.z),
                    1.0, 1e-12);
        EXPECT_NEAR(terraingen::Length(p), kEllipsoid.RadiusAt(point), 1e-9);
        const Vec2d back = kEllipsoid.Coordinates(p);
        EXPECT_NEAR(back.y, point.y, 1e-9);
    }
    EXPECT_NEAR(kEllipsoid.RadiusAt({0.0, 0.0}), 270.0, 1e-12);
    EXPECT_NEAR(kEllipsoid.RadiusAt({90.0, 0.0}), 147.0, 1e-12);
    EXPECT_NEAR(kEllipsoid.RadiusAt({0.0, 90.0}), 104.0, 1e-12);
}

TEST(Surface, EllipsoidFramesAreRightHandedWithUpAlongTheNormal) {
    const Vec3d radii = kEllipsoid.Radii();
    for (const Vec2d point : {Vec2d{40.0, -30.0}, Vec2d{-170.0, 60.0}, Vec2d{10.0, 89.0}}) {
        const terraingen::LocalFrame frame = kEllipsoid.Frame(point);
        EXPECT_NEAR(Dot3(frame.east, frame.east), 1.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.north, frame.north), 1.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.east, frame.north), 0.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.east, frame.up), 0.0, 1e-12);
        const Vec3d cross = terraingen::Cross(frame.east, frame.north);
        EXPECT_NEAR(Dot3(cross, frame.up), 1.0, 1e-12);
        const Vec3d p = kEllipsoid.Position(point);
        const Vec3d g{p.x / (radii.x * radii.x), p.y / (radii.y * radii.y),
                      p.z / (radii.z * radii.z)};
        EXPECT_NEAR(Dot3(g, frame.up), terraingen::Length(g), 1e-12) << "up is the normal";
        EXPECT_GT(Dot3(frame.north, Vec3d{0.0, 0.0, 1.0}), 0.0) << "north points poleward";
    }
}

TEST(Surface, EllipsoidOffsetsAndLocateInvertEachOther) {
    for (int i = 0; i < 400; ++i) {
        const Vec2d center{static_cast<double>(i * 53 % 360) - 180.0,
                           static_cast<double>(i * 17 % 160) - 80.0};
        const Vec2d offset{static_cast<double>(i % 21 - 10) * 3.0,
                           static_cast<double>(i % 17 - 8) * 3.0};
        const Vec2d point = kEllipsoid.Locate(center, offset);
        const Vec2d back = kEllipsoid.Offset(center, point);
        EXPECT_NEAR(back.x, offset.x, 1e-8) << center.x << ", " << center.y;
        EXPECT_NEAR(back.y, offset.y, 1e-8) << center.x << ", " << center.y;
    }
    EXPECT_EQ(kEllipsoid.Locate({12.0, 34.0}, {0.0, 0.0}), (Vec2d{12.0, 34.0}));
}

TEST(Surface, ShortEllipsoidOffsetsMatchTheDistance) {
    for (int i = 0; i < 200; ++i) {
        const Vec2d center{static_cast<double>(i * 29 % 360) - 180.0,
                           static_cast<double>(i * 11 % 160) - 80.0};
        const Vec2d point = kEllipsoid.Locate(center, {0.6, -0.8});
        const double distance = kEllipsoid.Distance(center, point);
        EXPECT_NEAR(distance, 1.0, 1e-3);
        EXPECT_DOUBLE_EQ(distance, kEllipsoid.Distance(point, center));
    }
}

TEST(Surface, EllipsoidExpandCoversEveryPointWithinTheDistance) {
    const Bounds2d region{20.0, -10.0, 30.0, 5.0};
    for (const double meters : {5.0, 40.0, 90.0}) {
        const Bounds2d expanded = kEllipsoid.Expand(region, meters);
        for (int i = 0; i < 20000; ++i) {
            const Vec2d point{static_cast<double>(i % 200) * 1.8 - 180.0,
                              static_cast<double>(i / 200) * 1.8 - 90.0};
            const Vec2d nearest{std::clamp(point.x, region.minX, region.maxX),
                                std::clamp(point.y, region.minY, region.maxY)};
            if (kEllipsoid.Distance(nearest, point) <= meters) {
                EXPECT_TRUE(kEllipsoid.Contains(expanded, point))
                    << meters << ": " << point.x << ", " << point.y;
            }
        }
    }
    const Bounds2d whole = kEllipsoid.Expand(region, 104.0);
    EXPECT_EQ(whole.minX, -180.0);
    EXPECT_EQ(whole.minY, -90.0);
}

TEST(Surface, EllipsoidAreaPerSteradianIntegratesToItsArea) {
    const double a = 3000.0;
    const double c = 1000.0;
    const Surface spheroid = Surface::Ellipsoid(a, a, c);
    const int steps = 720;
    const double step = terraingen::kPi / steps;
    double area = 0.0;
    for (int j = 0; j < steps; ++j) {
        const double lat = -90.0 + (j + 0.5) * 180.0 / steps;
        for (int i = 0; i < 2 * steps; ++i) {
            const double lon = -180.0 + (i + 0.5) * 180.0 / steps;
            area += spheroid.AreaPerSteradian({lon, lat}) *
                    std::cos(lat * terraingen::kPi / 180.0) * step * step;
        }
    }
    const double e = std::sqrt(1.0 - c * c / (a * a));
    const double expected =
        2.0 * terraingen::kPi * a * a * (1.0 + (1.0 - e * e) / e * std::atanh(e));
    EXPECT_NEAR(area / expected, 1.0, 1e-5);
}

namespace {

/// Every cell of every face at `cellSize`.
std::vector<CellIndex> AllCells(const CubeSphereCells& cells, double cellSize) {
    const std::int32_t n = cells.CellsPerEdge(cellSize);
    std::vector<CellIndex> all;
    for (std::int32_t i = 0; i < 6 * n; ++i) {
        for (std::int32_t j = 0; j < n; ++j) {
            all.push_back({i, j});
        }
    }
    return all;
}

}  // namespace

TEST(CubeSphereCells, EllipsoidCellsAreSizedByTheShortestAxisAndTileItsArea) {
    const double a = 3000.0;
    const double c = 1000.0;
    const Surface spheroid = Surface::Ellipsoid(a, a, c);
    const CubeSphereCells cells(spheroid);
    EXPECT_EQ(cells.Radius(), c);
    const double cellSize = 200.0;
    double total = 0.0;
    for (const CellIndex& cell : AllCells(cells, cellSize)) {
        total += cells.CellArea(cell, cellSize);
    }
    const double e = std::sqrt(1.0 - c * c / (a * a));
    const double expected =
        2.0 * terraingen::kPi * a * a * (1.0 + (1.0 - e * e) / e * std::atanh(e));
    EXPECT_NEAR(total / expected, 1.0, 1e-6);
    EXPECT_THROW(CubeSphereCells{Surface::Plane()}, std::invalid_argument);
}

TEST(CubeSphereCells, EllipsoidSamplingBoundsTheWeightOverEachCell) {
    for (const Surface& body :
         {Surface::Ellipsoid(270.0, 147.0, 104.0), Surface::Ellipsoid(10.0, 1.0, 1.0)}) {
        const CubeSphereCells cells(body);
        for (const double cellSize : {cells.Radius() * 2.0, cells.Radius() / 2.0}) {
            for (const CellIndex& cell : AllCells(cells, cellSize)) {
                const auto sampling = cells.CellSampling(cell, cellSize);
                ASSERT_GT(sampling.bound, 0.0);
                EXPECT_GE(sampling.area, cells.CellArea(cell, cellSize));
                for (int k = 0; k < 400; ++k) {
                    const double u = (k % 20 + 0.5) / 20.0;
                    const double v = (k / 20 + 0.5) / 20.0;
                    const Vec2d point = cells.SamplePoint(u, v, cell, cellSize);
                    EXPECT_LE(cells.Weight(point), sampling.bound) << cell.i << ", " << cell.j;
                }
            }
        }
    }
}

TEST(CubeSphereCells, SpheresDoNotThin) {
    const CubeSphereCells cells(Surface::Sphere(kMoon));
    const CellIndex cell{3, 2};
    const auto sampling = cells.CellSampling(cell, 50000.0);
    EXPECT_EQ(sampling.area, cells.CellArea(cell, 50000.0));
    EXPECT_EQ(sampling.bound, 0.0);
    EXPECT_EQ(CubeSphereCells(kMoon).CellArea(cell, 50000.0), sampling.area);
    EXPECT_EQ(terraingen::PlanarCells().CellSampling({1, 2}, 10.0).bound, 0.0);
    EXPECT_EQ(terraingen::PlanarCells().Weight({1.0, 2.0}), 1.0);
}

namespace {

/// A lumpy elongated body: an ellipsoid of semi-axes 270, 147 and 104 m with smooth bumps.
double Lumpy(const Vec3d& u) {
    const double x = u.x / 270.0;
    const double y = u.y / 147.0;
    const double z = u.z / 104.0;
    return (1.0 + 0.08 * std::sin(3.0 * u.x) * std::cos(2.0 * u.y + u.z)) /
           std::sqrt(x * x + y * y + z * z);
}

const Surface kShape = Surface::ShapeModel(
    std::make_shared<const terraingen::RadialShape>(terraingen::RadialShape::Sample(48, Lumpy)));

}  // namespace

TEST(Surface, ShapeModelsAreBodiesWithTheirShapesRadii) {
    EXPECT_TRUE(kShape.IsBody());
    EXPECT_FALSE(kShape.IsSphere());
    ASSERT_NE(kShape.Shape(), nullptr);
    EXPECT_EQ(kShape.MinRadius(), kShape.Shape()->MinRadius());
    EXPECT_EQ(kShape.MaxRadius(), kShape.Shape()->MaxRadius());
    EXPECT_GT(kShape.Radius(), kShape.MinRadius());
    EXPECT_LT(kShape.Radius(), kShape.MaxRadius());
    EXPECT_EQ(kShape, kShape);
    EXPECT_NE(kShape, kEllipsoid);
    EXPECT_TRUE(Surface::Sphere(kMoon).IsSphere());
    EXPECT_FALSE(kEllipsoid.IsSphere());
    EXPECT_FALSE(Surface::Plane().IsSphere());
    EXPECT_EQ(kEllipsoid.MinRadius(), 104.0);
    EXPECT_EQ(kEllipsoid.MaxRadius(), 270.0);
    EXPECT_EQ(Surface::Sphere(kMoon).MinRadius(), kMoon);
    EXPECT_THROW(Surface::ShapeModel(nullptr), std::invalid_argument);

    const Surface ball = Surface::ShapeModel(std::make_shared<const terraingen::RadialShape>(
        terraingen::RadialShape::Sample(8, [](const Vec3d&) { return 500.0; })));
    EXPECT_NEAR(ball.Radius(), 500.0, 1e-9) << "the volumetric mean radius";
    EXPECT_NEAR(ball.RadiusAt({33.0, -21.0}), 500.0, 1e-9);
}

TEST(Surface, ShapeModelPositionsLieOnTheShapeInTheirDirection) {
    for (int i = 0; i < 200; ++i) {
        const Vec2d point{static_cast<double>(i * 37 % 360) - 180.0,
                          static_cast<double>(i * 13 % 179) - 89.0};
        const Vec3d p = kShape.Position(point);
        const Vec3d u = terraingen::UnitVector(point.x, point.y);
        EXPECT_NEAR(terraingen::Length(p), kShape.Shape()->At(u), 1e-9);
        EXPECT_NEAR(terraingen::Length(p), kShape.RadiusAt(point), 1e-9);
        const Vec2d back = kShape.Coordinates(p);
        EXPECT_NEAR(back.y, point.y, 1e-9);
    }
}

TEST(Surface, ShapeModelFramesAreRightHandedWithUpAlongTheNormal) {
    for (const Vec2d point : {Vec2d{40.0, -30.0}, Vec2d{-170.0, 60.0}, Vec2d{10.0, 89.0}}) {
        const terraingen::LocalFrame frame = kShape.Frame(point);
        EXPECT_NEAR(Dot3(frame.east, frame.east), 1.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.north, frame.north), 1.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.east, frame.north), 0.0, 1e-12);
        EXPECT_NEAR(Dot3(frame.east, frame.up), 0.0, 1e-12);
        EXPECT_NEAR(Dot3(terraingen::Cross(frame.east, frame.north), frame.up), 1.0, 1e-12);
        const Vec3d p = kShape.Position(point);
        for (const Vec2d step : {Vec2d{0.01, 0.0}, Vec2d{0.0, 0.01}}) {
            const Vec3d q = kShape.Position({point.x + step.x, point.y + step.y});
            const Vec3d chord{q.x - p.x, q.y - p.y, q.z - p.z};
            EXPECT_LT(std::fabs(Dot3(chord, frame.up)), 2e-2 * terraingen::Length(chord))
                << "up is normal to the surface";
        }
        EXPECT_GT(Dot3(frame.up, p), 0.0) << "up points outward";
    }
}

TEST(Surface, ShapeModelOffsetsAndLocateInvertEachOther) {
    for (int i = 0; i < 400; ++i) {
        const Vec2d center{static_cast<double>(i * 53 % 360) - 180.0,
                           static_cast<double>(i * 17 % 160) - 80.0};
        const Vec2d offset{static_cast<double>(i % 21 - 10) * 3.0,
                           static_cast<double>(i % 17 - 8) * 3.0};
        const Vec2d point = kShape.Locate(center, offset);
        const Vec2d back = kShape.Offset(center, point);
        EXPECT_NEAR(back.x, offset.x, 1e-7) << center.x << ", " << center.y;
        EXPECT_NEAR(back.y, offset.y, 1e-7) << center.x << ", " << center.y;
    }
    EXPECT_EQ(kShape.Locate({12.0, 34.0}, {0.0, 0.0}), (Vec2d{12.0, 34.0}));
}

TEST(Surface, ShapeModelExpandCoversEveryPointWithinTheDistance) {
    const Bounds2d region{20.0, -10.0, 30.0, 5.0};
    for (const double meters : {5.0, 40.0}) {
        const Bounds2d expanded = kShape.Expand(region, meters);
        for (int i = 0; i < 20000; ++i) {
            const Vec2d point{static_cast<double>(i % 200) * 1.8 - 180.0,
                              static_cast<double>(i / 200) * 1.8 - 90.0};
            const Vec2d nearest{std::clamp(point.x, region.minX, region.maxX),
                                std::clamp(point.y, region.minY, region.maxY)};
            if (kShape.Distance(nearest, point) <= meters) {
                EXPECT_TRUE(kShape.Contains(expanded, point))
                    << meters << ": " << point.x << ", " << point.y;
            }
        }
    }
}

TEST(Surface, ShapeModelAreaPerSteradianIntegratesToTheEllipsoidsArea) {
    const double a = 3000.0;
    const double c = 1000.0;
    const Surface spheroid = Surface::ShapeModel(std::make_shared<const terraingen::RadialShape>(
        terraingen::RadialShape::Sample(96, [&](const Vec3d& u) {
            return 1.0 / std::sqrt((u.x * u.x + u.y * u.y) / (a * a) + u.z * u.z / (c * c));
        })));
    const int steps = 360;
    const double step = terraingen::kPi / steps;
    double area = 0.0;
    for (int j = 0; j < steps; ++j) {
        const double lat = -90.0 + (j + 0.5) * 180.0 / steps;
        for (int i = 0; i < 2 * steps; ++i) {
            const double lon = -180.0 + (i + 0.5) * 180.0 / steps;
            area += spheroid.AreaPerSteradian({lon, lat}) *
                    std::cos(lat * terraingen::kPi / 180.0) * step * step;
        }
    }
    const double e = std::sqrt(1.0 - c * c / (a * a));
    const double expected =
        2.0 * terraingen::kPi * a * a * (1.0 + (1.0 - e * e) / e * std::atanh(e));
    EXPECT_NEAR(area / expected, 1.0, 2e-3);
}

TEST(CubeSphereCells, ShapeModelCellsTileItsAreaAndBoundTheirWeight) {
    const CubeSphereCells cells(kShape);
    EXPECT_EQ(cells.Radius(), kShape.MinRadius());
    const double cellSize = cells.Radius() / 2.0;
    double total = 0.0;
    for (const CellIndex& cell : AllCells(cells, cellSize)) {
        total += cells.CellArea(cell, cellSize);
        const auto sampling = cells.CellSampling(cell, cellSize);
        EXPECT_EQ(sampling.bound, kShape.Shape()->MaxAreaPerSteradian());
        for (int k = 0; k < 100; ++k) {
            const Vec2d point =
                cells.SamplePoint((k % 10 + 0.5) / 10.0, (k / 10 + 0.5) / 10.0, cell, cellSize);
            EXPECT_LE(cells.Weight(point), sampling.bound);
        }
    }
    double expected = 0.0;
    const int steps = 360;
    const double step = terraingen::kPi / steps;
    for (int j = 0; j < steps; ++j) {
        const double lat = -90.0 + (j + 0.5) * 180.0 / steps;
        for (int i = 0; i < 2 * steps; ++i) {
            expected += kShape.AreaPerSteradian({-180.0 + (i + 0.5) * 180.0 / steps, lat}) *
                        std::cos(lat * terraingen::kPi / 180.0) * step * step;
        }
    }
    EXPECT_NEAR(total / expected, 1.0, 1e-3);
}
