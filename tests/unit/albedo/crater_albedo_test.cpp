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
        options.apply.irregularity.manual = true;
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
    options.minDiameter = 2000.0;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.maxDiameter = 500.0;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.minAgeGyr = 0.1;
    EXPECT_FLOAT_EQ(At(Grey(std::make_shared<CraterAlbedo>(craters, options)), 0.0, 0.0), 0.11f);
    options = NoRays();
    options.manual = false;
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
    options.minDiameter = 5000.0;
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
    rejects([](CraterAlbedoOptions& o) { o.minDiameter = 10.0, o.maxDiameter = 5.0; });
    rejects([](CraterAlbedoOptions& o) { o.minAgeGyr = -1.0; });
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
