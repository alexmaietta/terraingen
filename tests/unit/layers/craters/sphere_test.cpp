#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <vector>

using terraingen::Bounds2d;
using terraingen::Crater;
using terraingen::CraterLayer;
using terraingen::CraterLayerOptions;
using terraingen::CraterList;
using terraingen::CraterModels;
using terraingen::CubeSphereCells;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::Surface;
using terraingen::TerrainFeatures;

namespace {

constexpr double kMoon = 1737400.0;
constexpr double kDegree = std::numbers::pi / 180.0;

CraterModels Models() {
    return {std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
}

CraterLayerOptions SphereOptions() {
    CraterLayerOptions options;
    options.seed = 5;
    options.generation.dMin = 150.0;
    options.generation.dMax = 1200.0;
    options.surface = Surface::Sphere(kMoon);
    return options;
}

/// A longitude-latitude raster of `side` cells of `step` degrees from (`lon`, `lat`) at its
/// north-west corner.
Raster<float> Geographic(double lon, double lat, double step, std::size_t side) {
    Raster<float> raster(side, side, 0.0f);
    raster.SetTransform(GeoTransform::NorthUp(lon, lat, step));
    return raster;
}

double BoxArea(const Bounds2d& box) {
    return kMoon * kMoon * (box.maxX - box.minX) * kDegree *
           (std::sin(box.maxY * kDegree) - std::sin(box.minY * kDegree));
}

std::set<std::uint64_t> Ids(const CraterList& craters) {
    return {craters.Ids().begin(), craters.Ids().end()};
}

}  // namespace

TEST(SphereCraters, GeneratesTheDensityPerSquareMetreOfSphere) {
    const CraterModels models = Models();
    const CubeSphereCells cells(kMoon);
    for (const Bounds2d& box : {Bounds2d{10.0, 0.0, 10.6, 0.6}, Bounds2d{40.0, 70.0, 41.8, 70.6}}) {
        terraingen::CraterGenerationOptions options{.dMin = 150.0, .dMax = 1200.0};
        options.surface = Surface::Sphere(kMoon);
        const CraterList craters =
            terraingen::GenerateCraters(cells, box, *models.density, *models.ages, 9u, options);
        const double expected = models.density->ExpectedCount(BoxArea(box), 150.0, 1200.0);
        ASSERT_GT(expected, 500.0);
        EXPECT_NEAR(static_cast<double>(craters.Size()), expected, 4.0 * std::sqrt(expected));
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            EXPECT_TRUE(box.Contains(craters.X()[n], craters.Y()[n]));
        }
    }
}

TEST(SphereCraters, TilesAgreeAcrossTheAntimeridianAndThePole) {
    const CraterModels models = Models();
    const CubeSphereCells cells(kMoon);
    terraingen::CraterGenerationOptions options{.dMin = 150.0, .dMax = 1200.0, .marginRadii = 2.0};
    options.surface = Surface::Sphere(kMoon);
    const auto generate = [&](const Bounds2d& box) {
        return Ids(
            terraingen::GenerateCraters(cells, box, *models.density, *models.ages, 9u, options));
    };
    const std::set<std::uint64_t> whole = generate({179.8, -0.1, 180.2, 0.1});
    std::set<std::uint64_t> joined = generate({179.8, -0.1, 180.0, 0.1});
    const std::set<std::uint64_t> east = generate({-180.0, -0.1, -179.8, 0.1});
    joined.insert(east.begin(), east.end());
    EXPECT_FALSE(whole.empty());
    EXPECT_EQ(whole, joined);

    const std::set<std::uint64_t> cap = generate({-180.0, 89.9, 180.0, 90.0});
    std::set<std::uint64_t> quarters;
    for (const double lon : {-180.0, -90.0, 0.0, 90.0}) {
        const std::set<std::uint64_t> part = generate({lon, 89.9, lon + 90.0, 90.0});
        quarters.insert(part.begin(), part.end());
    }
    EXPECT_FALSE(cap.empty());
    EXPECT_EQ(cap, quarters);
}

TEST(SphereCraters, SpacingMeasuresGreatCircleDistances) {
    const Surface moon = Surface::Sphere(kMoon);
    const double metre = 1.0 / moon.UnitLength();
    const Crater a{0.0, 60.0, 100.0, 1.0, 1u};
    const Crater near{150.0 * metre / std::cos(60.0 * kDegree), 60.0, 100.0, 2.0, 2u};
    const Crater far{450.0 * metre / std::cos(60.0 * kDegree), 60.0, 100.0, 2.0, 3u};
    const terraingen::CraterSpacing spacing(2.0, 2.0);
    EXPECT_TRUE(terraingen::CratersConflict(a, near, spacing, moon));
    EXPECT_FALSE(terraingen::CratersConflict(a, far, spacing, moon));
    CraterList list;
    for (const Crater& crater : {a, near, far}) {
        list.Append(crater);
    }
    const CraterList thinned = terraingen::ThinBySpacing(list, spacing, moon);
    ASSERT_EQ(thinned.Size(), 2u);
    EXPECT_EQ(thinned.Ids()[0], 1u);
    EXPECT_EQ(thinned.Ids()[1], 3u);
}

TEST(SphereCraterLayer, ShapesCratersInMetresAroundTheirCentres) {
    CraterLayerOptions options = SphereOptions();
    options.generate = false;
    options.generation.manual.Append({30.0, 60.0, 2000.0, 1.0, 11u});
    const CraterLayer sphere(Models(), options);
    CraterLayerOptions flat = options;
    flat.surface = Surface::Plane();
    flat.generation.manual = {};
    flat.generation.manual.Append({0.0, 0.0, 2000.0, 1.0, 11u});
    const CraterLayer plane(Models(), flat);
    const Surface moon = Surface::Sphere(kMoon);
    for (const double distance : {0.0, 300.0, 990.0, 1300.0, 2500.0}) {
        for (const double azimuth : {0.3, 2.0, 4.4}) {
            const terraingen::Vec2d offset{distance * std::sin(azimuth),
                                           distance * std::cos(azimuth)};
            const terraingen::Vec2d at = moon.Locate({30.0, 60.0}, offset);
            EXPECT_NEAR(sphere.ApplyAt(0.0f, at.x, at.y, {5.0}),
                        plane.ApplyAt(0.0f, offset.x, offset.y, {5.0}), 1e-4f)
                << distance;
        }
    }
}

TEST(SphereCraterLayer, GridsEqualPointsAndThreadsAcrossTheAntimeridian) {
    CraterLayerOptions options = SphereOptions();
    options.apply.irregularity.enabled = true;
    options.apply.irregularity.elongatedShare = 0.5;
    options.generation.spacing = terraingen::CraterSpacing(1.5, 1.0);
    options.generation.manual.Append({179.99, 45.01, 3000.0, 0.5, 21u});
    const CraterLayer layer(Models(), options);
    Raster<float> one = Geographic(179.95, 45.05, 0.001, 100);
    Raster<float> many = one;
    TerrainFeatures features;
    layer.Apply(one, {.spacing = 0.0, .threads = 1}, features);
    TerrainFeatures more;
    layer.Apply(many, {.spacing = 0.0, .threads = 0}, more);
    int changed = 0;
    for (std::size_t i = 0; i < one.Size(); ++i) {
        EXPECT_EQ(one.Values()[i], many.Values()[i]);
        changed += one.Values()[i] != 0.0f ? 1 : 0;
    }
    EXPECT_GT(changed, 1000);
    EXPECT_GT(features.craters.Size(), 1u);
    const double spacing = terraingen::RasterGrid::Of(one).Spacing(options.surface);
    EXPECT_NEAR(spacing, 0.001 * options.surface.UnitLength(), 1e-9);
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{10, 10}, std::pair<std::size_t, std::size_t>{50, 50},
          std::pair<std::size_t, std::size_t>{95, 40},
          std::pair<std::size_t, std::size_t>{70, 77}}) {
        const auto centre = one.CellCenter(col, row);
        const double lon = centre.x > 180.0 ? centre.x - 360.0 : centre.x;
        EXPECT_EQ(layer.ApplyAt(0.0f, lon, centre.y, {spacing}), one(col, row))
            << col << " " << row;
    }
    Raster<float> wrapped = Geographic(-180.05, 45.05, 0.001, 100);
    TerrainFeatures again;
    layer.Apply(wrapped, {.spacing = 0.0, .threads = 2}, again);
    for (std::size_t i = 0; i < one.Size(); ++i) {
        EXPECT_NEAR(wrapped.Values()[i], one.Values()[i], 1e-2f);
    }
    const auto centre = wrapped.CellCenter(3, 50);
    EXPECT_EQ(wrapped(3, 50), layer.ApplyAt(0.0f, centre.x, centre.y, {spacing}));
}

TEST(SphereCraterLayer, FitsDatumsOnTheSphere) {
    CraterLayerOptions options = SphereOptions();
    options.generate = false;
    options.apply.datum = terraingen::CraterDatum::Plane;
    options.generation.manual.Append({-20.0, -30.0, 2000.0, 1.0, 31u});
    const CraterLayer layer(Models(), options);
    class Tilted final : public terraingen::Ground {
      public:
        float HeightAt(double x, double y, double) const override {
            return static_cast<float>(1000.0 * (x + 20.0) + 300.0 * (y + 30.0));
        }
    } ground;
    Raster<float> heights = Geographic(-20.05, -29.95, 0.002, 50);
    for (std::size_t row = 0; row < 50; ++row) {
        for (std::size_t col = 0; col < 50; ++col) {
            const auto c = heights.CellCenter(col, row);
            heights(col, row) = ground.HeightAt(c.x, c.y, 0.0);
        }
    }
    const Raster<float> before = heights;
    TerrainFeatures features;
    const double spacing = terraingen::RasterGrid::Of(heights).Spacing(options.surface);
    layer.Apply(heights, {.spacing = spacing, .threads = 1, .ground = &ground}, features);
    const auto centre = heights.CellCenter(25, 25);
    EXPECT_EQ(heights(25, 25), layer.ApplyAt(before(25, 25), centre.x, centre.y,
                                             {.spacing = spacing, .ground = &ground}));
    EXPECT_LT(heights(25, 25), before(25, 25) - 100.0f);
}
