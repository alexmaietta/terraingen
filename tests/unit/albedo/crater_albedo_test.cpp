#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Albedo;
using terraingen::AlbedoSample;
using terraingen::Crater;
using terraingen::CraterAlbedo;
using terraingen::CraterAlbedoOptions;
using terraingen::CraterRays;
using terraingen::EvaluateAlbedo;
using terraingen::EvaluateAlbedoAt;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::RayRetention;
using terraingen::ReflectanceAtMaturity;
using terraingen::Surface;
using terraingen::SyntheticAlbedo;
using terraingen::SyntheticAlbedoOptions;
using terraingen::ValueDistribution;

namespace {

/// Craters with no generation, only `manual` ones.
std::shared_ptr<const terraingen::CraterLayer> Craters(const std::vector<Crater>& manual,
                                                       bool irregular = false) {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const auto& crater : manual) {
        options.generation.manual.Append(crater);
    }
    if (irregular) {
        options.apply.irregularity.enabled = true;
        options.apply.irregularity.selection.manual = true;
        options.apply.irregularity.elongatedShare = 1.0;
        options.apply.irregularity.ellipticity = {ValueDistribution::Kind::Constant, 1.5};
        options.apply.irregularity.heading = 0.0;
    }
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                 std::make_shared<terraingen::FreshAges>(),
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     std::make_shared<terraingen::UniformAges>(1.0),
                                     terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

CraterAlbedoOptions NoRays() {
    CraterAlbedoOptions options;
    options.rays.enabled = false;
    return options;
}

Albedo Grey(std::shared_ptr<const terraingen::AlbedoLayer> layer, double value = 0.11) {
    SyntheticAlbedoOptions params;
    params.value = value;
    return {std::make_shared<SyntheticAlbedo>(params), {std::move(layer)}};
}

float At(const Albedo& albedo, double x, double y, double spacing = 1.0) {
    return EvaluateAlbedoAt(albedo, x, y, {spacing})[0];
}

float Brightened(double background, double maturity) {
    return static_cast<float>(ReflectanceAtMaturity(background, 0.202, maturity));
}

}  // namespace

TEST(RayRetention, FollowsWernerAndMedvedev) {
    const RayRetention retention;
    EXPECT_EQ(retention.Myr(500.0), 5.0);
    EXPECT_EQ(retention.Myr(1000.0), 5.0);
    EXPECT_NEAR(retention.Myr(std::sqrt(5.0) * 1000.0), std::sqrt(5.0 * 750.0), 1e-9);
    EXPECT_EQ(retention.Myr(5000.0), 750.0);
    EXPECT_EQ(retention.Myr(50000.0), 750.0);
    RayRetention custom;
    custom.largeMyr = 2000.0;
    custom.scale = 2.0;
    EXPECT_EQ(custom.Myr(60000.0), 4000.0);
    EXPECT_EQ(custom.Myr(20000.0), 1500.0);
}

TEST(CraterRays, MedianLengthFollowsElliott) {
    const CraterRays rays;
    EXPECT_NEAR(rays.MedianLength(2000.0), 6590.0, 1e-9);
    EXPECT_NEAR(rays.MedianLength(84000.0), 1000.0 * 6.59 * std::pow(42.0, 1.27), 1e-6);
}

TEST(CraterAlbedo, BrightensAFreshCraterAsTychoIs) {
    const auto layer =
        std::make_shared<CraterAlbedo>(Craters({{0.0, 0.0, 1000.0, 0.0, 7u, true}}), NoRays());
    const Albedo albedo = Grey(layer);
    EXPECT_FLOAT_EQ(At(albedo, 0.0, 0.0), Brightened(0.11, 0.334));
    EXPECT_FLOAT_EQ(At(albedo, 300.0, 0.0), Brightened(0.11, 0.334));
    EXPECT_NEAR(At(albedo, 500.5, 0.0), Brightened(0.11, 0.278), 1e-4);
    const double edge = std::pow(3.0, -3.0);
    const double ejecta = 0.076 * (std::pow(1.5, -3.0) - edge) / (1.0 - edge);
    EXPECT_NEAR(At(albedo, 0.0, -750.0), Brightened(0.11, 0.202 + ejecta), 1e-7);
    EXPECT_FLOAT_EQ(At(albedo, 1600.0, 0.0), 0.11f);
    EXPECT_EQ(layer->Reach({0.0, 0.0, 1000.0, 0.0, 7u, true}), 1500.0);
}

TEST(CraterAlbedo, FadesWithAgeOverTheRetentionTime) {
    CraterAlbedoOptions options = NoRays();
    const auto halfway =
        std::make_shared<CraterAlbedo>(Craters({{0.0, 0.0, 10000.0, 0.375, 7u, true}}), options);
    EXPECT_NEAR(At(Grey(halfway), 0.0, 0.0, 10.0), Brightened(0.11, 0.202 + 0.066), 1e-7);
    options.fadeExponent = 2.0;
    const auto squared =
        std::make_shared<CraterAlbedo>(Craters({{0.0, 0.0, 10000.0, 0.375, 7u, true}}), options);
    EXPECT_NEAR(At(Grey(squared), 0.0, 0.0, 10.0), Brightened(0.11, 0.202 + 0.033), 1e-7);
    const auto old =
        std::make_shared<CraterAlbedo>(Craters({{0.0, 0.0, 10000.0, 0.8, 7u, true}}), NoRays());
    EXPECT_FLOAT_EQ(At(Grey(old), 0.0, 0.0, 10.0), 0.11f);
    const auto small =
        std::make_shared<CraterAlbedo>(Craters({{0.0, 0.0, 500.0, 0.01, 7u, true}}), NoRays());
    EXPECT_FLOAT_EQ(At(Grey(small), 0.0, 0.0), 0.11f);
    EXPECT_EQ(small->Reach({0.0, 0.0, 500.0, 0.01, 7u, true}), 0.0);
}

TEST(CraterAlbedo, KeepsOnlyEligibleCraters) {
    const auto craters = Craters({{0.0, 0.0, 1000.0, 0.0, 7u, true}});
    CraterAlbedoOptions options = NoRays();
    options.selection.minDiameter = 2000.0;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.selection.maxDiameter = 500.0;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.selection.minAgeGyr = 0.1;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.selection.manual = false;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.share = 0.0;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.maxMaturity = 0.25;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0),
                    Brightened(0.11, 0.25));
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, NoRays())), 0.0, 0.0, 600.0),
                    0.11f);
}

TEST(CraterAlbedo, RaysReachBeyondTheEjecta) {
    const Crater crater{0.0, 0.0, 2000.0, 0.0, 11u, true};
    CraterAlbedoOptions options;
    options.rays.patchiness = 0.0;
    options.rays.count = {ValueDistribution::Kind::Constant, 12.0};
    options.rays.oblique.enabled = false;
    const auto layer = std::make_shared<CraterAlbedo>(Craters({crater}), options);
    const double reach = layer->Reach(crater);
    EXPECT_GT(reach, 0.70 * 6590.0 - 1.0);
    EXPECT_LT(reach, 1.60 * 6590.0 + 1.0);
    int bright = 0;
    int total = 0;
    for (int step = 0; step < 720; ++step) {
        const double azimuth = 2.0 * terraingen::kPi * step / 720.0;
        const double excess =
            layer->Excess(crater, 3500.0 * std::cos(azimuth), 3500.0 * std::sin(azimuth), 1.0);
        EXPECT_GE(excess, 0.0);
        EXPECT_LE(excess, 12.0 * 0.048);
        bright += excess > 0.0 ? 1 : 0;
        ++total;
    }
    EXPECT_GT(bright, total / 20);
    EXPECT_LT(bright, total);
    EXPECT_EQ(layer->Excess(crater, 0.0, 1.6 * 6590.0 + 10.0, 1.0), 0.0);

    CraterAlbedoOptions patchy = options;
    patchy.rays.patchiness = 1.0;
    const auto broken = std::make_shared<CraterAlbedo>(Craters({crater}), patchy);
    double plain = 0.0;
    double patched = 0.0;
    for (int step = 0; step < 720; ++step) {
        const double azimuth = 2.0 * terraingen::kPi * step / 720.0;
        plain += layer->Excess(crater, 3500.0 * std::cos(azimuth), 3500.0 * std::sin(azimuth), 1.0);
        patched +=
            broken->Excess(crater, 3500.0 * std::cos(azimuth), 3500.0 * std::sin(azimuth), 1.0);
    }
    EXPECT_LT(patched, plain);
    EXPECT_GT(patched, 0.0);
}

TEST(CraterAlbedo, FollowsIrregularOutlines) {
    const Crater crater{0.0, 0.0, 1000.0, 0.0, 7u, true};
    const auto layer = std::make_shared<CraterAlbedo>(Craters({crater}, true), NoRays());
    const double scale = std::sqrt(1.5);
    EXPECT_GT(layer->Excess(crater, 0.95 * 500.0 * scale, 0.0, 1.0), 0.1);
    EXPECT_LT(layer->Excess(crater, 0.0, 0.95 * 500.0, 1.0), 0.1);
    EXPECT_GE(layer->Reach(crater), 1500.0 * scale);
}

TEST(CraterAlbedo, GridsTilesAndPointsAgree) {
    std::vector<Crater> craters{{0.0, 0.0, 400.0, 0.0, 1u, true},
                                {350.0, 120.0, 300.0, 0.001, 2u, true},
                                {-200.0, -260.0, 600.0, 0.002, 3u, true}};
    const auto layer = std::make_shared<CraterAlbedo>(Craters(craters));
    SyntheticAlbedoOptions params;
    params.rgb = true;
    params.saturation = 0.1;
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params), {layer}};
    const RasterGrid whole{64, 64, GeoTransform::NorthUp(-640.0, 640.0, 20.0)};
    const RasterGrid tile{16, 16, GeoTransform::NorthUp(-320.0, 320.0, 20.0)};
    const auto grid = EvaluateAlbedo(albedo, whole, {20.0, 1});
    const auto threaded = EvaluateAlbedo(albedo, whole, {20.0, 0});
    const auto part = EvaluateAlbedo(albedo, tile, {20.0});
    int brightened = 0;
    for (std::size_t row = 0; row < 64; ++row) {
        for (std::size_t col = 0; col < 64; ++col) {
            for (std::size_t band = 0; band < 3; ++band) {
                EXPECT_EQ(grid[band](col, row), threaded[band](col, row));
            }
            brightened += grid[0](col, row) > 0.1101f ? 1 : 0;
        }
    }
    EXPECT_GT(brightened, 100);
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            EXPECT_EQ(part[0](col, row), grid[0](col + 16, row + 16));
        }
    }
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{32, 32}, std::pair<std::size_t, std::size_t>{49, 25},
          std::pair<std::size_t, std::size_t>{20, 45}}) {
        const auto centre = grid[0].CellCenter(col, row);
        const AlbedoSample point = EvaluateAlbedoAt(albedo, centre.x, centre.y, {20.0});
        for (std::size_t band = 0; band < 3; ++band) {
            EXPECT_EQ(point[band], grid[band](col, row));
        }
    }
}

TEST(CraterAlbedo, SkipsNoDataAndEmptyBandsAndIneligibleCraters) {
    const Crater crater{0.0, 0.0, 1000.0, 0.0, 7u, true};
    CraterAlbedoOptions options = NoRays();
    options.selection.minDiameter = 5000.0;
    EXPECT_EQ(CraterAlbedo(Craters({crater}), options).Excess(crater, 0.0, 0.0, 1.0), 0.0);
    const CraterAlbedo layer(Craters({crater}), NoRays());
    AlbedoSample value{NAN, 0.2f, 0.0f};
    layer.ApplyAt(value, 2, 0.0, 0.0, {1.0});
    EXPECT_TRUE(std::isnan(value[0]));
    EXPECT_FLOAT_EQ(value[1], Brightened(0.2, 0.334));
    std::vector<terraingen::Raster<float>> none;
    layer.Apply(none, {1.0});
    terraingen::ForEachAlbedoCell(none, 1, [](AlbedoSample&, double, double) { FAIL(); });
    EXPECT_TRUE(none.empty());
}

TEST(CraterAlbedo, RejectsInvalidOptions) {
    const auto craters = Craters({});
    EXPECT_THROW(CraterAlbedo(nullptr), std::invalid_argument);
    const auto rejects = [&](auto change) {
        CraterAlbedoOptions options;
        change(options);
        EXPECT_THROW(CraterAlbedo(craters, options), std::invalid_argument);
    };
    rejects(
        [](CraterAlbedoOptions& o) { o.rays.count = {ValueDistribution::Kind::Constant, 65.0}; });
    rejects(
        [](CraterAlbedoOptions& o) { o.rays.count = {ValueDistribution::Kind::Constant, -1.0}; });
    rejects([](CraterAlbedoOptions& o) {
        o.rays.widthDegrees = {ValueDistribution::Kind::Constant, 180.0};
    });
    rejects([](CraterAlbedoOptions& o) { o.rays.lengthCoefficient = 0.0; });
    rejects([](CraterAlbedoOptions& o) { o.rays.excess = -0.1; });
    rejects([](CraterAlbedoOptions& o) { o.rays.patchiness = 1.5; });
    rejects([](CraterAlbedoOptions& o) { o.rays.patchRadii = 0.0; });
    rejects([](CraterAlbedoOptions& o) { o.retention.midDiameter = 500.0; });
    rejects([](CraterAlbedoOptions& o) { o.retention.smallMyr = 0.0; });
    rejects([](CraterAlbedoOptions& o) { o.fade.endSamples = 1.0; });
    rejects([](CraterAlbedoOptions& o) {
        o.selection.minDiameter = 10.0;
        o.selection.maxDiameter = 5.0;
    });
    rejects([](CraterAlbedoOptions& o) { o.selection.minAgeGyr = -1.0; });
    rejects([](CraterAlbedoOptions& o) { o.share = 1.5; });
    rejects([](CraterAlbedoOptions& o) { o.backgroundMaturity = 0.0; });
    rejects([](CraterAlbedoOptions& o) { o.maxMaturity = 0.1; });
    rejects([](CraterAlbedoOptions& o) { o.interiorExcess = -0.1; });
    rejects([](CraterAlbedoOptions& o) { o.ejectaExponent = 0.0; });
    rejects([](CraterAlbedoOptions& o) { o.ejectaReachRadii = 1.0; });
    rejects([](CraterAlbedoOptions& o) { o.fadeExponent = 0.0; });
    EXPECT_THROW(terraingen::EvaluateAlbedoAt(Grey(std::make_shared<CraterAlbedo>(craters)), 0.0,
                                              0.0, {0.0}),
                 std::invalid_argument);
}

TEST(CraterAlbedo, WorksOnASphereInMetres) {
    const Surface moon = Surface::Sphere(1737400.0);
    terraingen::CraterLayerOptions sphereOptions;
    sphereOptions.generate = false;
    sphereOptions.surface = moon;
    sphereOptions.generation.manual.Append({179.99, -20.0, 2000.0, 0.0, 9u, true});
    const auto sphereCraters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::FreshAges>(),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        sphereOptions);
    const CraterAlbedo sphere(sphereCraters);
    const CraterAlbedo plane(Craters({{0.0, 0.0, 2000.0, 0.0, 9u, true}}));
    const Crater onSphere = sphereCraters->Options().generation.manual.At(0);
    const Crater onPlane{0.0, 0.0, 2000.0, 0.0, 9u, true};
    for (const double distance : {0.0, 600.0, 1400.0, 2500.0, 5000.0}) {
        for (const double azimuth : {0.1, 1.9, 3.7, 5.2}) {
            const terraingen::Vec2d offset{distance * std::sin(azimuth),
                                           distance * std::cos(azimuth)};
            const terraingen::Vec2d at = moon.Locate({179.99, -20.0}, offset);
            EXPECT_NEAR(sphere.Excess(onSphere, at.x, at.y, 1.0),
                        plane.Excess(onPlane, offset.x, offset.y, 1.0), 1e-6);
        }
    }
    SyntheticAlbedoOptions params;
    params.value = 0.11;
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params, terraingen::Vec2d{}, moon),
                        {std::make_shared<CraterAlbedo>(sphereCraters)},
                        moon};
    const double step = 50.0 / moon.UnitLength();
    const RasterGrid grid{120, 80,
                          GeoTransform::NorthUp(179.99 - 60.0 * step, -20.0 + 40.0 * step, step)};
    const auto bands = EvaluateAlbedo(albedo, grid, {0.0, 0});
    int bright = 0;
    for (std::size_t row = 0; row < 80; ++row) {
        for (std::size_t col = 0; col < 120; ++col) {
            bright += bands[0](col, row) > 0.1101f ? 1 : 0;
        }
    }
    EXPECT_GT(bright, 100);
    for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{60, 40},
                                   std::pair<std::size_t, std::size_t>{100, 10}}) {
        const auto centre = bands[0].CellCenter(col, row);
        EXPECT_EQ(EvaluateAlbedoAt(albedo, centre.x, centre.y, {50.0})[0], bands[0](col, row));
    }
}

TEST(CraterAlbedo, DrawsImpactAnglesAsShoemakerFound) {
    const CraterAlbedo layer(Craters({}));
    int asymmetric = 0;
    int forbidden = 0;
    int butterfly = 0;
    const int craters = 20000;
    for (int n = 0; n < craters; ++n) {
        const auto impact =
            layer.Impact({0.0, 0.0, 1000.0, 0.0, static_cast<std::uint64_t>(n + 1)});
        EXPECT_GE(impact.angleDegrees, 0.0);
        EXPECT_LE(impact.angleDegrees, 90.0);
        asymmetric += impact.angleDegrees < 45.0 ? 1 : 0;
        forbidden += impact.angleDegrees < 25.0 ? 1 : 0;
        butterfly += impact.angleDegrees < 10.0 ? 1 : 0;
        if (impact.angleDegrees < 45.0) {
            EXPECT_GE(impact.speedRatio, 1.2);
            EXPECT_LE(impact.speedRatio, 1.4);
        } else {
            EXPECT_EQ(impact.speedRatio, 1.0);
        }
    }
    const auto share = [&](int count) { return static_cast<double>(count) / craters; };
    const auto expected = [](double degrees) {
        const double s = std::sin(degrees * terraingen::kPi / 180.0);
        return s * s;
    };
    EXPECT_NEAR(share(asymmetric), expected(45.0), 0.015);
    EXPECT_NEAR(share(forbidden), expected(25.0), 0.01);
    EXPECT_NEAR(share(butterfly), expected(10.0), 0.005);
    CraterAlbedoOptions vertical;
    vertical.rays.oblique.enabled = false;
    EXPECT_EQ(CraterAlbedo(Craters({}), vertical).Impact({0.0, 0.0, 1.0, 0.0, 3u}).angleDegrees,
              90.0);
}

TEST(CraterAlbedo, ObliqueImpactsLengthenRaysDownrangeAndClearThemUprange) {
    CraterAlbedoOptions options;
    options.rays.patchiness = 0.0;
    options.rays.count = {ValueDistribution::Kind::Constant, 64.0};
    options.rays.widthDegrees = {ValueDistribution::Kind::Constant, 6.0};
    options.rays.lengthSpread = {ValueDistribution::Kind::Constant, 0.0};
    options.rays.oblique.heading = 0.0;
    double downrange = 0.0;
    double uprange = 0.0;
    int forbiddenCraters = 0;
    for (std::uint64_t id = 1; id <= 300; ++id) {
        const Crater crater{0.0, 0.0, 2000.0, 0.0, id, true};
        const CraterAlbedo layer(Craters({crater}), options);
        const auto impact = layer.Impact(crater);
        const auto reachAlong = [&](double azimuth) {
            for (double r = 12000.0; r > 3100.0; r -= 50.0) {
                if (layer.Excess(crater, r * std::cos(azimuth), r * std::sin(azimuth), 1.0) > 0.0) {
                    return r;
                }
            }
            return 0.0;
        };
        double east = 0.0;
        double west = 0.0;
        for (const double offset : {-0.3, -0.15, 0.0, 0.15, 0.3}) {
            east = std::fmax(east, reachAlong(offset));
            west = std::fmax(west, reachAlong(terraingen::kPi + offset));
        }
        if (impact.angleDegrees < 25.0) {
            ++forbiddenCraters;
            EXPECT_EQ(west, 0.0) << id;
        } else if (impact.angleDegrees < 45.0) {
            downrange += east;
            uprange += west;
        }
    }
    EXPECT_GT(forbiddenCraters, 20);
    EXPECT_GT(downrange, 1.2 * uprange);
    EXPECT_GT(CraterAlbedo(Craters({}), options).Reach({0.0, 0.0, 2000.0, 0.0, 5u, true}), 0.0);
}

TEST(ObliqueImpacts, RejectsInvalidSettings) {
    const auto rejects = [](auto change) {
        terraingen::ObliqueImpacts oblique;
        change(oblique);
        EXPECT_THROW(terraingen::ValidateObliqueImpacts(oblique), std::invalid_argument);
    };
    rejects([](auto& o) { o.downrangeSpeedRatio = {ValueDistribution::Kind::Constant, 0.9}; });
    rejects(
        [](auto& o) { o.downrangeSpeedRatio = {ValueDistribution::Kind::LogNormal, 1.3, 0.1}; });
    rejects([](auto& o) { o.butterflyDegrees = 30.0; });
    rejects([](auto& o) { o.asymmetricDegrees = 95.0; });
    rejects([](auto& o) { o.zoneHalfWidthDegrees = 200.0; });
    rejects([](auto& o) { o.heading = NAN; });
    terraingen::ObliqueImpacts off;
    off.enabled = false;
    EXPECT_EQ(off.MaxStretch(), 1.0);
    EXPECT_NEAR(terraingen::ObliqueImpacts{}.MaxStretch(),
                (1.0 + 0.4 / 2.4) * (1.0 + 0.4 / 2.4) / (1.0 + 0.5 * (0.4 / 2.4) * (0.4 / 2.4)),
                1e-12);
}

TEST(CraterAlbedo, TexturesFreshMaterialIntoFilamentsLobesAndBrightWalls) {
    const Crater crater{0.0, 0.0, 400.0, 0.0, 7u, true};
    CraterAlbedoOptions plain = NoRays();
    CraterAlbedoOptions textured = NoRays();
    textured.texture.streaks = 0.9;
    textured.texture.continuousRadii = 0.5;
    textured.texture.lobes = 0.4;
    textured.texture.floorShare = 0.3;
    const CraterAlbedo smooth(Craters({crater}), plain);
    const CraterAlbedo filaments(Craters({crater}), textured);
    double least = 1e9;
    double most = 0.0;
    for (int k = 0; k < 720; ++k) {
        const double angle = 2.0 * terraingen::kPi * k / 720.0;
        const double share =
            filaments.Excess(crater, 360.0 * std::cos(angle), 360.0 * std::sin(angle), 1.0) /
            smooth.Excess(crater, 360.0 * std::cos(angle), 360.0 * std::sin(angle), 1.0);
        least = std::fmin(least, share);
        most = std::fmax(most, share);
    }
    EXPECT_LT(least, 0.6) << "dark gaps between filaments, or a lobe drawn in";
    EXPECT_GT(most, 1.0) << "a lobe pushed out";
    CraterAlbedoOptions collar = textured;
    collar.texture.lobes = 0.0;
    const CraterAlbedo continuous(Craters({crater}), collar);
    for (int k = 0; k < 36; ++k) {
        const double angle = 2.0 * terraingen::kPi * k / 36.0;
        const double x = 201.0 * std::cos(angle);
        const double y = 201.0 * std::sin(angle);
        ASSERT_GT(continuous.Excess(crater, x, y, 1.0), 0.95 * smooth.Excess(crater, x, y, 1.0))
            << "continuous at the rim";
    }
    EXPECT_LT(filaments.Excess(crater, 0.0, 0.0, 1.0), smooth.Excess(crater, 0.0, 0.0, 1.0))
        << "a dimmer floor";
    EXPECT_GT(filaments.Reach(crater), smooth.Reach(crater)) << "lobes reach further";

    const auto layer = std::make_shared<CraterAlbedo>(Craters({crater}), textured);
    const Albedo albedo = Grey(layer);
    const RasterGrid whole{48, 48, GeoTransform::NorthUp(-480.0, 480.0, 20.0)};
    const RasterGrid tile{16, 16, GeoTransform::NorthUp(-160.0, 160.0, 20.0)};
    const auto grid = EvaluateAlbedo(albedo, whole, {20.0});
    const auto part = EvaluateAlbedo(albedo, tile, {20.0});
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            ASSERT_EQ(part[0](col, row), grid[0](col + 16, row + 16));
        }
    }
    textured.texture.lobes = 0.95;
    EXPECT_THROW(terraingen::ValidateCraterAlbedoOptions(textured), std::invalid_argument);
    textured.texture = {};
    textured.texture.continuousRadii = -1.0;
    EXPECT_THROW(terraingen::ValidateCraterAlbedoTexture(textured.texture), std::invalid_argument);
}

namespace {

/// Zones on every crater with fixed changes and reaches, round and sharp-edged.
CraterAlbedoOptions FixedZones() {
    CraterAlbedoOptions options = NoRays();
    options.interiorExcess = 0.0;
    options.ejectaExcess = 0.0;
    options.zones.enabled = true;
    terraingen::CraterReflectanceZones& z = options.zones;
    for (terraingen::ReflectanceZone* zone :
         {&z.proximalHigh, &z.proximalLow, &z.distalHigh, &z.distalLow}) {
        zone->share = 1.0;
        zone->scale = {ValueDistribution::Kind::Constant, 1.0};
        zone->raggedness = 0.0;
        zone->feather = 0.1;
    }
    z.proximalHigh.change = {ValueDistribution::Kind::Constant, 0.12};
    z.proximalLow.change = {ValueDistribution::Kind::Constant, -0.09};
    z.distalHigh.change = {ValueDistribution::Kind::Constant, 0.03};
    z.distalLow.change = {ValueDistribution::Kind::Constant, -0.04};
    return options;
}

}  // namespace

TEST(CraterAlbedo, ReflectanceZonesAreOffByDefault) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    const CraterAlbedo layer(Craters({crater}), NoRays());
    EXPECT_FALSE(CraterAlbedoOptions{}.zones.enabled);
    EXPECT_EQ(layer.ReflectanceChange(crater, 40.0, 0.0, 0.1), 0.0);
}

TEST(CraterAlbedo, ReflectanceZonesNestFromTheCraterOut) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    const CraterAlbedo layer(Craters({crater}), FixedZones());
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 0.0, 0.0, 0.1), 0.12) << "PHRZ to 1.5 D";
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 0.0, 40.0, 0.1), -0.09) << "PLRZ to 2.9 D";
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 300.0, 0.0, 0.1), 0.03)
        << "DHRZ to 0.52 D^2.5 = 930 m";
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, -1500.0, 0.0, 0.1), -0.04)
        << "DLRZ to 5.03 D^2 = 2012 m";
    EXPECT_EQ(layer.ReflectanceChange(crater, 2100.0, 0.0, 0.1), 0.0);
    EXPECT_NEAR(layer.Reach(crater), 5.03 * 400.0, 1e-9);
    EXPECT_NEAR(FixedZones().zones.MaxReach(20.0), 5.03 * 400.0, 1e-9);
}

TEST(CraterAlbedo, OnlyCratersReachingImmatureRegolithGetABrightProximalZone) {
    const Crater shallow{0.0, 0.0, 5.0, 0.0, 7u, true};
    const CraterAlbedo layer(Craters({shallow}), FixedZones());
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(shallow, 0.0, 0.0, 0.1), -0.09)
        << "dark ejecta from mature regolith";
    CraterAlbedoOptions thin = FixedZones();
    thin.zones.matureDepth = 0.2;
    EXPECT_DOUBLE_EQ(
        CraterAlbedo(Craters({shallow}), thin).ReflectanceChange(shallow, 0.0, 0.0, 0.1), 0.12);
    const Crater large{0.0, 0.0, 80.0, 0.0, 8u, true};
    EXPECT_EQ(CraterAlbedo(Craters({large}), FixedZones()).ReflectanceChange(large, 0.0, 0.0, 0.1),
              0.0)
        << "beyond the largest diameter";
    const Crater old{0.0, 0.0, 20.0, 1.0, 9u, true};
    EXPECT_EQ(CraterAlbedo(Craters({old}), FixedZones()).ReflectanceChange(old, 0.0, 0.0, 0.1), 0.0)
        << "faded with the bright material";
}

TEST(CraterAlbedo, ReflectanceZonesFollowTheirSharesAndRaggedEdges) {
    CraterAlbedoOptions options = FixedZones();
    options.zones.distalHigh.share = 0.0;
    options.zones.distalLow.share = 0.0;
    std::vector<Crater> craters;
    for (std::uint64_t id = 1; id <= 40; ++id) {
        craters.push_back({0.0, 0.0, 20.0, 0.0, id, true});
    }
    const CraterAlbedo none(Craters(craters), options);
    for (const Crater& crater : craters) {
        ASSERT_EQ(none.ReflectanceChange(crater, 300.0, 0.0, 0.1), 0.0);
    }
    options.zones.distalLow.share = 0.5;
    const CraterAlbedo half(Craters(craters), options);
    int dark = 0;
    for (const Crater& crater : craters) {
        dark += half.ReflectanceChange(crater, 1000.0, 0.0, 0.1) < 0.0 ? 1 : 0;
    }
    EXPECT_GT(dark, 8);
    EXPECT_LT(dark, 32);

    options.zones.distalLow.share = 1.0;
    options.zones.distalLow.raggedness = 0.6;
    const CraterAlbedo ragged(Craters({craters[0]}), options);
    int covered = 0;
    for (int k = 0; k < 360; ++k) {
        const double angle = 2.0 * terraingen::kPi * k / 360.0;
        covered += ragged.ReflectanceChange(craters[0], 1500.0 * std::cos(angle),
                                            1500.0 * std::sin(angle), 0.1) < 0.0
                       ? 1
                       : 0;
    }
    EXPECT_GT(covered, 20) << "fingers reaching out";
    EXPECT_LT(covered, 340) << "gaps between them";
}

TEST(CraterAlbedo, ReflectanceZonesDarkenAndBrightenTheAlbedoAlike) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    const auto layer = std::make_shared<CraterAlbedo>(Craters({crater}), FixedZones());
    const Albedo albedo = Grey(layer);
    EXPECT_FLOAT_EQ(At(albedo, 0.0, 0.0, 0.1), static_cast<float>(0.11 * 1.12));
    EXPECT_FLOAT_EQ(At(albedo, 0.0, 40.0, 0.1), static_cast<float>(0.11 * 0.91));
    const RasterGrid whole{48, 48, GeoTransform::NorthUp(-120.0, 120.0, 5.0)};
    const RasterGrid tile{16, 16, GeoTransform::NorthUp(-40.0, 40.0, 5.0)};
    const auto grid = EvaluateAlbedo(albedo, whole, {0.1});
    const auto part = EvaluateAlbedo(albedo, tile, {0.1});
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            ASSERT_EQ(part[0](col, row), grid[0](col + 16, row + 16));
            const auto centre = part[0].CellCenter(col, row);
            ASSERT_EQ(At(albedo, centre.x, centre.y, 0.1), part[0](col, row));
        }
    }
}

TEST(CraterReflectanceZones, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateCraterReflectanceZones({}));
    const auto rejects = [](auto change) {
        terraingen::CraterReflectanceZones zones;
        change(zones);
        EXPECT_THROW(terraingen::ValidateCraterReflectanceZones(zones), std::invalid_argument);
        CraterAlbedoOptions options;
        options.zones = zones;
        EXPECT_THROW(terraingen::ValidateCraterAlbedoOptions(options), std::invalid_argument);
    };
    using Zones = terraingen::CraterReflectanceZones;
    rejects([](Zones& z) { z.maxDiameter = 0.0; });
    rejects([](Zones& z) { z.excavationRatio = 0.0; });
    rejects([](Zones& z) { z.matureDepth = -1.0; });
    rejects([](Zones& z) { z.distalLow.share = 1.5; });
    rejects([](Zones& z) { z.distalLow.change = {ValueDistribution::Kind::Constant, -1.0}; });
    rejects([](Zones& z) { z.distalLow.change = {ValueDistribution::Kind::Normal, 0.0, 0.1}; });
    rejects([](Zones& z) { z.proximalLow.coefficient = 0.0; });
    rejects([](Zones& z) { z.proximalLow.exponent = 0.0; });
    rejects(
        [](Zones& z) { z.proximalHigh.scale = {ValueDistribution::Kind::LogNormal, 1.0, 0.5}; });
    rejects([](Zones& z) { z.proximalHigh.scale = {ValueDistribution::Kind::Constant, 0.0}; });
    rejects([](Zones& z) { z.distalHigh.raggedness = 1.0; });
    rejects([](Zones& z) { z.distalHigh.feather = 0.0; });
    rejects([](Zones& z) { z.distalHigh.fingerWidthDegrees = 0.0; });
}

TEST(CraterAlbedo, ThinZonesLastAsLongAsTheTopCentimetresTakeToChurn) {
    const Crater recent{0.0, 0.0, 20.0, 0.00004, 7u, true};
    const CraterAlbedo layer(Craters({recent}), FixedZones());
    EXPECT_NEAR(layer.ReflectanceChange(recent, -1500.0, 0.0, 0.1), -0.04 * (1.0 - 40.0 / 81.0),
                1e-12);
    const Crater older{0.0, 0.0, 20.0, 0.0001, 7u, true};
    const CraterAlbedo faded(Craters({older}), FixedZones());
    EXPECT_EQ(faded.ReflectanceChange(older, 300.0, 0.0, 0.1), 0.0);
    EXPECT_EQ(faded.ReflectanceChange(older, -1500.0, 0.0, 0.1), 0.0);
    EXPECT_EQ(faded.ReflectanceChange(older, 0.0, 40.0, 0.1), 0.0) << "the thin dark ejecta too";
    EXPECT_NEAR(faded.ReflectanceChange(older, 0.0, 0.0, 0.1), 0.12 * (1.0 - 0.1 / 5.0), 1e-12)
        << "the immature ejecta fade with the bright material";
    EXPECT_LT(faded.Reach(older), 100.0);
    CraterAlbedoOptions lasting = FixedZones();
    lasting.zones.distalLow.retentionMyr.reset();
    EXPECT_LT(CraterAlbedo(Craters({older}), lasting).ReflectanceChange(older, -1500.0, 0.0, 0.1),
              0.0);
    lasting.zones.distalLow.retentionMyr = 0.0;
    EXPECT_THROW(terraingen::ValidateCraterAlbedoOptions(lasting), std::invalid_argument);
}

TEST(CraterAlbedo, TextureStreaksBreakTheBrightProximalZoneIntoFilaments) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    CraterAlbedoOptions options = FixedZones();
    options.texture.streaks = 0.9;
    const CraterAlbedo layer(Craters({crater}), options);
    double least = 1.0;
    double most = 0.0;
    for (int k = 0; k < 720; ++k) {
        const double angle = 2.0 * terraingen::kPi * k / 720.0;
        const double change =
            layer.ReflectanceChange(crater, 12.0 * std::cos(angle), 12.0 * std::sin(angle), 0.1);
        least = std::fmin(least, change);
        most = std::fmax(most, change);
    }
    EXPECT_LT(least, 0.06);
    EXPECT_GT(most, 0.11);
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 0.0, 40.0, 0.1), -0.09) << "PLRZ even";
}

TEST(CraterAlbedo, ReflectanceZonesGatherSmallCratersFromAsFarAsTheyReach) {
    terraingen::CraterLayerOptions layerOptions;
    layerOptions.generation.dMin = 4.0;
    layerOptions.generation.dMax = 64.0;
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.001, 2.0),
                                 std::make_shared<terraingen::FreshAges>(),
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     std::make_shared<terraingen::UniformAges>(1.0),
                                     terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        layerOptions);
    CraterAlbedoOptions options = NoRays();
    options.interiorExcess = 0.0;
    options.ejectaExcess = 0.0;
    options.zones.enabled = true;
    options.zones.maxDiameter = 20.0;
    const auto layer = std::make_shared<CraterAlbedo>(craters, options);
    const terraingen::CraterList all = craters->Craters({-2500.0, -2500.0, 2500.0, 2500.0}, 0.5);
    for (const auto& [x, y] : {std::pair{0.0, 0.0}, std::pair{310.0, -120.0}}) {
        double change = 0.0;
        for (std::size_t n = 0; n < all.Size(); ++n) {
            change += layer->ReflectanceChange(all.At(n), x, y, 0.5);
        }
        ASSERT_NE(change, 0.0);
        AlbedoSample value{0.11f, 0.0f, 0.0f};
        layer->ApplyAt(value, 1, x, y, {0.5});
        EXPECT_FLOAT_EQ(value[0], static_cast<float>(0.11 * (1.0 + change)));
    }
}

namespace {

/// Flat ground with a ridge 5 m high from x = 100 to 110 m.
struct Ridge final : terraingen::Ground {
    float HeightAt(double x, double, double) const override {
        return x >= 100.0 && x < 110.0 ? 5.0f : 0.0f;
    }
};

}  // namespace

TEST(CraterAlbedo, RisesShadowTheDistalZonesBehindThem) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    CraterAlbedoOptions options = FixedZones();
    options.zones.shadows.enabled = true;
    const CraterAlbedo layer(Craters({crater}), options);
    const Ridge ridge;
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 150.0, 0.0, 0.1), 0.03) << "no ground";
    EXPECT_NEAR(layer.ReflectanceChange(crater, 150.0, 0.0, 0.1, &ridge), 0.0, 1e-12)
        << "45 m behind a 5 m ridge, under a jet dropping 3 degrees";
    EXPECT_NEAR(layer.ReflectanceChange(crater, 350.0, 0.0, 0.1, &ridge), 0.03, 1e-12)
        << "past the shadow, 5 m / tan 3 deg = 95 m long";
    EXPECT_NEAR(layer.ReflectanceChange(crater, 0.0, 150.0, 0.1, &ridge), 0.03, 1e-12)
        << "beside the ridge";
    EXPECT_NEAR(layer.ReflectanceChange(crater, 80.0, 0.0, 0.1, &ridge), 0.03, 1e-12)
        << "in front of it";
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 150.0, 0.0, 0.1, &ridge),
                     layer.ReflectanceChange(crater, 150.0, 0.0, 0.1, &ridge));
    CraterAlbedoOptions steeper = options;
    steeper.zones.shadows.jetAngleDegrees = 30.0;
    EXPECT_NEAR(
        CraterAlbedo(Craters({crater}), steeper).ReflectanceChange(crater, 150.0, 0.0, 0.1, &ridge),
        0.03, 1e-12)
        << "a steep jet drops back to the ground within 10 m";

    const auto shared = std::make_shared<CraterAlbedo>(Craters({crater}), options);
    const Albedo albedo = Grey(shared);
    const RasterGrid whole{40, 40, GeoTransform::NorthUp(80.0, 100.0, 5.0)};
    const RasterGrid tile{8, 8, GeoTransform::NorthUp(120.0, 60.0, 5.0)};
    terraingen::AlbedoContext onRidge;
    onRidge.spacing = 1.0;
    onRidge.ground = &ridge;
    const auto grid = EvaluateAlbedo(albedo, whole, onRidge);
    const auto part = EvaluateAlbedo(albedo, tile, onRidge);
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 0; col < 8; ++col) {
            ASSERT_EQ(part[0](col, row), grid[0](col + 8, row + 8));
            const auto centre = part[0].CellCenter(col, row);
            ASSERT_EQ(EvaluateAlbedoAt(albedo, centre.x, centre.y, onRidge)[0], part[0](col, row));
        }
    }
    EXPECT_EQ(grid[0](10, 20), 0.11f) << "(132.5, 0) in shadow";
    EXPECT_GT(grid[0](39, 20), 0.11f) << "(277.5, 0) in the open";

    using Shadows = terraingen::ZoneShadows;
    std::vector<Shadows> invalidShadows(5);
    invalidShadows[0].jetAngleDegrees = 0.0;
    invalidShadows[1].jetAngleDegrees = 90.0;
    invalidShadows[2].depth = 0.0;
    invalidShadows[3].azimuths = 4;
    invalidShadows[4].radialSamples = 9000;
    for (const auto& bad : invalidShadows) {
        CraterAlbedoOptions invalid;
        invalid.zones.shadows = bad;
        EXPECT_THROW(terraingen::ValidateCraterAlbedoOptions(invalid), std::invalid_argument);
    }
}

namespace {

/// Flat ground with no data from x = 100 to 110 m.
struct Gap final : terraingen::Ground {
    float HeightAt(double x, double, double) const override {
        return x >= 100.0 && x < 110.0 ? NAN : 0.0f;
    }
};

}  // namespace

TEST(CraterAlbedo, ShadowsSkipTheRimNoDataAndCratersWithoutDistalZones) {
    const Crater crater{0.0, 0.0, 20.0, 0.0, 7u, true};
    CraterAlbedoOptions options = FixedZones();
    options.zones.shadows.enabled = true;
    const CraterAlbedo layer(Craters({crater}), options);
    const Ridge ridge;
    const Gap gap;
    EXPECT_DOUBLE_EQ(layer.ReflectanceChange(crater, 0.0, 0.0, 0.1, &ridge), 0.12) << "inside";
    EXPECT_NEAR(layer.ReflectanceChange(crater, 150.0, 0.0, 0.1, &gap), 0.03, 1e-12)
        << "no data casts no shadow";
    options.zones.distalHigh.share = 0.0;
    options.zones.distalLow.share = 0.0;
    EXPECT_DOUBLE_EQ(
        CraterAlbedo(Craters({crater}), options).ReflectanceChange(crater, 40.0, 0.0, 0.1, &ridge),
        -0.09);
}

TEST(CraterAlbedo, RaysCanFollowASharedLayout) {
    const Crater crater{0.0, 0.0, 400.0, 0.0, 7u, true};
    CraterAlbedoOptions options;
    options.interiorExcess = 0.0;
    options.ejectaExcess = 0.0;
    options.rays.patchiness = 0.0;
    options.rays.oblique.enabled = false;
    options.rays.lengthSpread = {ValueDistribution::Kind::Constant, 0.0};
    options.rays.layout = terraingen::RayLayout{3};
    const CraterAlbedo layer(Craters({crater}), options);
    const auto directions = terraingen::RayDirections(crater, *options.rays.layout);
    ASSERT_FALSE(directions.empty());
    for (const auto& ray : directions) {
        const double r = 600.0;
        EXPECT_GT(layer.Excess(crater, r * std::cos(ray.azimuth), r * std::sin(ray.azimuth), 1.0),
                  0.0)
            << "a ray along each shared direction";
    }
    int lit = 0;
    for (int k = 0; k < 720; ++k) {
        const double a = 2.0 * terraingen::kPi * k / 720.0;
        lit += layer.Excess(crater, 600.0 * std::cos(a), 600.0 * std::sin(a), 1.0) > 0.0 ? 1 : 0;
    }
    EXPECT_LT(lit, 700) << "and dark between them";
    options.rays.layout->widthDegrees = {ValueDistribution::Kind::Constant, 200.0};
    EXPECT_THROW(terraingen::ValidateCraterAlbedoOptions(options), std::invalid_argument);
}
