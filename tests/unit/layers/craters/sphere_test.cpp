// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/terraingen.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
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
using terraingen::RasterGrid;
using terraingen::Surface;

namespace {

constexpr double kMoon = 1737400.0;
constexpr double kDegree = terraingen::kPi / 180.0;

CraterModels Models() {
    return {std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
}

CraterLayerOptions SphereOptions() {
    CraterLayerOptions options;
    options.seed = 5;
    options.generation.minDiameter = 150.0;
    options.generation.maxDiameter = 1200.0;
    return options;
}

/// A longitude-latitude raster of `side` cells of `step` degrees from (`lon`, `lat`) at its
/// north-west corner.
Raster<float> Geographic(double lon, double lat, double step, std::size_t side) {
    Raster<float> raster(RasterGrid{side, side, GeoTransform::NorthUp(lon, lat, step)}, 0.0f);
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

TEST(SphereCraters, GeneratesTheDensityPerSquareMeterOfSphere) {
    const CraterModels models = Models();
    const CubeSphereCells cells(kMoon);
    for (const Bounds2d& box : {Bounds2d{10.0, 0.0, 10.6, 0.6}, Bounds2d{40.0, 70.0, 41.8, 70.6}}) {
        terraingen::CraterGenerationOptions options{150.0, 1200.0};
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
    terraingen::CraterGenerationOptions options{150.0, 1200.0, 2.0};
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
    const double meter = 1.0 / moon.UnitLength();
    const Crater a{0.0, 60.0, 100.0, 1.0, 1u};
    const Crater near{150.0 * meter / std::cos(60.0 * kDegree), 60.0, 100.0, 2.0, 2u};
    const Crater far{450.0 * meter / std::cos(60.0 * kDegree), 60.0, 100.0, 2.0, 3u};
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

TEST(SphereCraterLayer, ShapesCratersInMetersAroundTheirCenters) {
    CraterLayerOptions options = SphereOptions();
    options.generateRandom = false;
    options.generation.manual.Append({30.0, 60.0, 2000.0, 1.0, 11u});
    const CraterLayer sphere(Models(), options, Surface::Sphere(kMoon));
    CraterLayerOptions flat = options;
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
    const CraterLayer layer(Models(), options, Surface::Sphere(kMoon));
    Raster<float> one = Geographic(179.95, 45.05, 0.001, 100);
    Raster<float> many = one;
    layer.Apply(one, {0.0, 1});
    layer.Apply(many, {0.0, 0});
    int changed = 0;
    for (std::size_t i = 0; i < one.Size(); ++i) {
        EXPECT_EQ(one.Values()[i], many.Values()[i]);
        changed += one.Values()[i] != 0.0f ? 1 : 0;
    }
    EXPECT_GT(changed, 1000);
    const double spacing = one.Grid().Spacing(layer.Options().apply.surface);
    EXPECT_GT(layer.Craters(one.Grid().Bounds(), spacing).Size(), 1u);
    EXPECT_NEAR(spacing, 0.001 * layer.Options().apply.surface.UnitLength(), 1e-9);
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{10, 10}, std::pair<std::size_t, std::size_t>{50, 50},
          std::pair<std::size_t, std::size_t>{95, 40},
          std::pair<std::size_t, std::size_t>{70, 77}}) {
        const auto center = one.CellCenter(col, row);
        const double lon = center.x > 180.0 ? center.x - 360.0 : center.x;
        EXPECT_EQ(layer.ApplyAt(0.0f, lon, center.y, {spacing}), one(col, row))
            << col << " " << row;
    }
    Raster<float> wrapped = Geographic(-180.05, 45.05, 0.001, 100);
    layer.Apply(wrapped, {0.0, 2});
    for (std::size_t i = 0; i < one.Size(); ++i) {
        EXPECT_NEAR(wrapped.Values()[i], one.Values()[i], 1e-2f);
    }
    const auto center = wrapped.CellCenter(3, 50);
    EXPECT_EQ(wrapped(3, 50), layer.ApplyAt(0.0f, center.x, center.y, {spacing}));
}

TEST(SphereCraterLayer, FitsDatumsOnTheSphere) {
    CraterLayerOptions options = SphereOptions();
    options.generateRandom = false;
    options.apply.datum = terraingen::CraterDatum::Plane;
    options.generation.manual.Append({-20.0, -30.0, 2000.0, 1.0, 31u});
    const CraterLayer layer(Models(), options, Surface::Sphere(kMoon));
    class Tilted final : public terraingen::Ground {
      protected:
        void FillSamples(terraingen::SampleBlock& samples, double,
                         unsigned /*threads*/) const override {
            for (std::size_t i = 0; i < samples.Size(); ++i) {
                const terraingen::Vec2d p = samples.Position(i);
                samples.Height(i) =
                    static_cast<float>(1000.0 * (p.x + 20.0) + 300.0 * (p.y + 30.0));
            }
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
    const double spacing = heights.Grid().Spacing(layer.Options().apply.surface);
    layer.Apply(heights, {spacing, 1, &ground});
    const auto center = heights.CellCenter(25, 25);
    EXPECT_EQ(heights(25, 25),
              layer.ApplyAt(before(25, 25), center.x, center.y, {spacing, 1, &ground}));
    EXPECT_LT(heights(25, 25), before(25, 25) - 100.0f);
}

namespace {

/// A flat ground at height 0, which craters with a datum read beneath them.
class FlatGround final : public terraingen::Ground {
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = 0.0f;
        }
    }
};

/// A rimless parabolic bowl the crater kernel does not know, so it tabulates it.
class Bowl final : public terraingen::CraterProfile {
  public:
    double Height(double distance, double diameter,
                  const terraingen::CraterState& state) const override {
        const double x = distance / (0.5 * diameter);
        return x < 1.0 ? -state.current.depthRatio * diameter * (1.0 - x * x) : 0.0;
    }
    double ReachRadii() const override {
        return 1.0;
    }
};

struct BodyCase {
    const char* name;
    Surface surface;
    Crater crater;  ///< At the center of the raster.
    double step;    ///< Raster cell side, in degrees.
};

std::vector<BodyCase> Bodies() {
    const auto asteroid = std::make_shared<const terraingen::RadialShape>(
        terraingen::RadialShape::Sample(24, [](const terraingen::Vec3d& d) {
            return 8000.0 + 600.0 * d.x * d.y + 300.0 * d.z * d.z;
        }));
    return {{"plane", Surface::Plane(), {100.0, -50.0, 300.0, 1.0, 10u}, 12.0},
            {"sphere", Surface::Sphere(kMoon), {30.0, 60.0, 2000.0, 1.0, 11u}, 0.0008},
            {"ellipsoid",
             Surface::Ellipsoid(1738100.0, 1737400.0, 1735970.0),
             {-40.0, 25.0, 2000.0, 1.0, 12u},
             0.0008},
            {"shape model", Surface::ShapeModel(asteroid), {10.0, 20.0, 600.0, 1.0, 13u}, 0.05}};
}

/// Checks that `layer` gives a grid of 96 cells of `body.step` degrees centered on the crater the
/// same heights as its tiles, its points, other thread counts and a GPU backend, and digs a bowl.
void ExpectGridsTilesAndPointsAgree(const CraterLayer& layer, const BodyCase& body,
                                    const terraingen::Ground* ground) {
    const double half = 48.0 * body.step;
    Raster<float> whole = Geographic(body.crater.x - half, body.crater.y + half, body.step, 96);
    Raster<float> threaded = whole;
    const double spacing = whole.Grid().Spacing(body.surface);
    layer.Apply(whole, {spacing, 1, ground});
    layer.Apply(threaded, {spacing, 0, ground});
    for (std::size_t i = 0; i < whole.Size(); ++i) {
        ASSERT_EQ(whole.Values()[i], threaded.Values()[i]) << body.name;
    }
    EXPECT_LT(whole(48, 48), -0.05f * static_cast<float>(body.crater.diameter)) << body.name;
    Raster<float> tile = Geographic(body.crater.x - half + 32.0 * body.step,
                                    body.crater.y + half - 32.0 * body.step, body.step, 32);
    layer.Apply(tile, {spacing, 1, ground});
    for (std::size_t row = 0; row < 32; ++row) {
        for (std::size_t col = 0; col < 32; ++col) {
            ASSERT_EQ(tile(col, row), whole(col + 32, row + 32)) << body.name;
        }
    }
    for (const std::size_t at :
         {std::size_t{5}, std::size_t{40}, std::size_t{48}, std::size_t{70}}) {
        const auto center = whole.CellCenter(at, 95 - at);
        EXPECT_EQ(layer.ApplyAt(0.0f, center.x, center.y, {spacing, 1, ground}), whole(at, 95 - at))
            << body.name << " at " << at;
    }
    for (const terraingen::BackendStatus& status : terraingen::Backends()) {
        if (status.backend == terraingen::Backend::Cpu || !status.available) {
            continue;
        }
        Raster<float> gpu = Geographic(body.crater.x - half, body.crater.y + half, body.step, 96);
        layer.Apply(gpu, {spacing, 1, ground, status.backend});
        const auto tolerance = static_cast<float>(1e-5 * body.crater.diameter);
        for (std::size_t i = 0; i < whole.Size(); ++i) {
            ASSERT_NEAR(gpu.Values()[i], whole.Values()[i], tolerance)
                << body.name << " on " << terraingen::BackendName(status.backend);
        }
    }
}

}  // namespace

TEST(BodyCraterLayer, GridsTilesPointsAndBackendsAgreeOnEveryBody) {
    const FlatGround ground;
    for (const BodyCase& body : Bodies()) {
        for (const auto datum : {terraingen::CraterDatum::None, terraingen::CraterDatum::Plane,
                                 terraingen::CraterDatum::CenterHeight}) {
            CraterLayerOptions options;
            options.generateRandom = false;
            options.apply.datum = datum;
            options.apply.noise.amplitude = 0.005;
            options.apply.irregularity.enabled = true;
            options.apply.irregularity.selection.manual = true;
            options.apply.irregularity.elongatedShare = 1.0;
            options.generation.manual.Append(body.crater);
            const terraingen::Vec2d beside = body.surface.Locate({body.crater.x, body.crater.y},
                                                                 {0.3 * body.crater.diameter, 0.0});
            options.generation.manual.Append(
                {beside.x, beside.y, 0.4 * body.crater.diameter, 0.5, body.crater.id + 100u});
            const CraterLayer layer(Models(), options, body.surface);
            ExpectGridsTilesAndPointsAgree(layer, body, &ground);
        }
    }
}

TEST(BodyCraterLayer, ShapesCratersInTheirLocalFrameOnEveryBody) {
    for (const BodyCase& body : Bodies()) {
        CraterLayerOptions options;
        options.generateRandom = false;
        options.generation.manual.Append(body.crater);
        const CraterLayer layer(Models(), options, body.surface);
        CraterLayerOptions flat = options;
        flat.generation.manual = {};
        flat.generation.manual.Append({0.0, 0.0, body.crater.diameter, 1.0, body.crater.id});
        const CraterLayer plane(Models(), flat);
        for (const double distance : {0.0, 0.2, 0.49, 0.65}) {
            for (const double azimuth : {0.3, 2.0, 4.4}) {
                const double meters = distance * body.crater.diameter;
                const terraingen::Vec2d offset{meters * std::sin(azimuth),
                                               meters * std::cos(azimuth)};
                const terraingen::Vec2d at =
                    body.surface.Locate({body.crater.x, body.crater.y}, offset);
                EXPECT_NEAR(layer.ApplyAt(0.0f, at.x, at.y, {5.0}),
                            plane.ApplyAt(0.0f, offset.x, offset.y, {5.0}),
                            1e-4 * body.crater.diameter)
                    << body.name << " at " << distance;
            }
        }
    }
}

TEST(BodyCraterLayer, ShapesProfilesOfYourOwnOnEveryBody) {
    const FlatGround ground;
    for (const BodyCase& body : Bodies()) {
        CraterLayerOptions options;
        options.generateRandom = false;
        options.apply.datum = terraingen::CraterDatum::Plane;
        options.generation.manual.Append(body.crater);
        CraterModels models = Models();
        models.profile = std::make_shared<Bowl>();
        const CraterLayer layer(models, options, body.surface);
        ExpectGridsTilesAndPointsAgree(layer, body, &ground);
        const double depth =
            terraingen::LunarFreshCraterMorphometry(body.crater.diameter).depthRatio *
            body.crater.diameter;
        EXPECT_NEAR(layer.ApplyAt(0.0f, body.crater.x, body.crater.y, {5.0, 1, &ground}), -depth,
                    1e-3 * depth)
            << body.name;
    }
}
