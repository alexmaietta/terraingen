#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
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
using terraingen::SyntheticAlbedoParams;
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
    SyntheticAlbedoParams params;
    params.value = value;
    return {std::make_shared<SyntheticAlbedo>(params), {std::move(layer)}};
}

float At(const Albedo& albedo, double x, double y, double spacing = 1.0) {
    return EvaluateAlbedoAt(albedo, x, y, {.spacing = spacing})[0];
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
        const double azimuth = 2.0 * M_PI * step / 720.0;
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
        const double azimuth = 2.0 * M_PI * step / 720.0;
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
    SyntheticAlbedoParams params;
    params.rgb = true;
    params.saturation = 0.1;
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params), {layer}};
    const RasterGrid whole{64, 64, GeoTransform::NorthUp(-640.0, 640.0, 20.0)};
    const RasterGrid tile{16, 16, GeoTransform::NorthUp(-320.0, 320.0, 20.0)};
    const auto grid = EvaluateAlbedo(albedo, whole, {.spacing = 20.0, .threads = 1});
    const auto threaded = EvaluateAlbedo(albedo, whole, {.spacing = 20.0, .threads = 0});
    const auto part = EvaluateAlbedo(albedo, tile, {.spacing = 20.0});
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
        const AlbedoSample point = EvaluateAlbedoAt(albedo, centre.x, centre.y, {.spacing = 20.0});
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
    layer.ApplyAt(value, 2, 0.0, 0.0, {.spacing = 1.0});
    EXPECT_TRUE(std::isnan(value[0]));
    EXPECT_FLOAT_EQ(value[1], Brightened(0.2, 0.334));
    std::vector<terraingen::Raster<float>> none;
    layer.Apply(none, {.spacing = 1.0});
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
    EXPECT_THROW(Grey(std::make_shared<CraterAlbedo>(craters))
                     .layers[0]
                     ->ApplyAt(*std::make_unique<AlbedoSample>(), 1, 0.0, 0.0, {.spacing = 0.0}),
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
    SyntheticAlbedoParams params;
    params.value = 0.11;
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params, terraingen::Vec2d{}, moon),
                        {std::make_shared<CraterAlbedo>(sphereCraters)},
                        moon};
    const double step = 50.0 / moon.UnitLength();
    const RasterGrid grid{120, 80,
                          GeoTransform::NorthUp(179.99 - 60.0 * step, -20.0 + 40.0 * step, step)};
    const auto bands = EvaluateAlbedo(albedo, grid, {.spacing = 0.0, .threads = 0});
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
        EXPECT_EQ(EvaluateAlbedoAt(albedo, centre.x, centre.y, {.spacing = 50.0})[0],
                  bands[0](col, row));
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
        const double s = std::sin(degrees * M_PI / 180.0);
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
            west = std::fmax(west, reachAlong(M_PI + offset));
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
    textured.texture = {.streaks = 0.9, .continuousRadii = 0.5, .lobes = 0.4, .floorShare = 0.3};
    const CraterAlbedo smooth(Craters({crater}), plain);
    const CraterAlbedo filaments(Craters({crater}), textured);
    double least = 1e9;
    double most = 0.0;
    for (int k = 0; k < 720; ++k) {
        const double angle = 2.0 * std::numbers::pi * k / 720.0;
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
        const double angle = 2.0 * std::numbers::pi * k / 36.0;
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
    const auto grid = EvaluateAlbedo(albedo, whole, {.spacing = 20.0});
    const auto part = EvaluateAlbedo(albedo, tile, {.spacing = 20.0});
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            ASSERT_EQ(part[0](col, row), grid[0](col + 16, row + 16));
        }
    }
    textured.texture.lobes = 0.95;
    EXPECT_THROW(terraingen::ValidateCraterAlbedo(textured), std::invalid_argument);
    textured.texture = {.continuousRadii = -1.0};
    EXPECT_THROW(terraingen::ValidateCraterAlbedoTexture(textured.texture), std::invalid_argument);
}
