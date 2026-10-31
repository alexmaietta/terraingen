// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/splotches.h"

#include "terraingen/albedo/synthetic.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Albedo;
using terraingen::AlbedoSample;
using terraingen::Bounds2d;
using terraingen::EvaluateAlbedo;
using terraingen::EvaluateAlbedoAt;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::Splotch;
using terraingen::SplotchAlbedo;
using terraingen::SplotchAlbedoOptions;
using terraingen::Surface;
using terraingen::SyntheticAlbedo;
using terraingen::SyntheticAlbedoOptions;
using terraingen::ValueDistribution;

namespace {

Albedo Gray(SplotchAlbedoOptions options, Surface surface = {}) {
    SyntheticAlbedoOptions params;
    params.value = 0.11;
    return {std::make_shared<SyntheticAlbedo>(params),
            {std::make_shared<SplotchAlbedo>(options)},
            surface};
}

Splotch Dark(double diameter, double ageGyr) {
    Splotch splotch;
    splotch.mark = {0.0, 0.0, diameter, ageGyr, 3u, false};
    splotch.change = -0.04;
    splotch.otherChange = -0.04;
    return splotch;
}

}  // namespace

TEST(SplotchAlbedoOptions, FollowSpeyerersFormationRate) {
    const SplotchAlbedoOptions options;
    const double perKm2 = options.Coefficient() * std::pow(10.0, -options.rateExponent) * 1e6;
    EXPECT_NEAR(perKm2, 0.0029 * 81000.0, 0.02 * 0.0029 * 81000.0)
        << "0.0029 splotches over 10 m per km^2 per year, each lasting 8.1e4 years";
    EXPECT_NEAR(options.Coefficient() * 1e6 / options.lifetimeMyr / 1e6, 40.2, 0.5)
        << "40.2 over 1 m per km^2 per year";
}

TEST(SplotchAlbedo, GeneratesTheExpectedNumbersKindsAndAges) {
    SplotchAlbedoOptions options;
    options.minDiameter = 10.0;
    const SplotchAlbedo layer(options);
    const auto large = layer.Splotches({0.0, 0.0, 1000.0, 1000.0}, 1.0);
    EXPECT_GT(large.size(), 180u);
    EXPECT_LT(large.size(), 300u);

    options.minDiameter = 2.0;
    const auto all = SplotchAlbedo(options).Splotches({0.0, 0.0, 300.0, 300.0}, 0.5);
    ASSERT_GT(all.size(), 3000u);
    int dark = 0;
    int bright = 0;
    for (std::size_t n = 0; n < all.size(); ++n) {
        const Splotch& s = all[n];
        ASSERT_GE(s.mark.ageGyr, 0.0);
        ASSERT_LE(s.mark.ageGyr, 1e-3 * options.lifetimeMyr);
        ASSERT_GE(s.mark.diameter, 2.0);
        ASSERT_LT(s.mark.diameter, 40.0);
        if (n > 0) {
            ASSERT_LE(all[n - 1].mark.ageGyr, s.mark.ageGyr) << "youngest first";
        }
        if (s.change == s.otherChange) {
            (s.change < 0.0 ? dark : bright) += 1;
        } else {
            ASSERT_DOUBLE_EQ(s.change, -0.04);
            ASSERT_DOUBLE_EQ(s.otherChange, 0.10);
        }
    }
    const double count = static_cast<double>(all.size());
    EXPECT_NEAR(dark / count, 0.907, 0.02);
    EXPECT_NEAR(bright / count, 0.074, 0.015);
    options.minDiameter = 1.0;
    for (const Splotch& s : SplotchAlbedo(options).Splotches({0.0, 0.0, 50.0, 50.0}, 0.9)) {
        ASSERT_GT(s.mark.diameter, 1.8) << "unresolved at 0.9 m";
    }
}

TEST(SplotchAlbedo, ChangesFadeWithAgeAndEndAtTheEdge) {
    const SplotchAlbedo layer;
    EXPECT_DOUBLE_EQ(layer.Change(Dark(10.0, 0.0), 0.0, 0.0, 0.1), -0.04);
    EXPECT_NEAR(layer.Change(Dark(10.0, 0.5 * 0.081e-3), 1.0, 0.0, 0.1), -0.02, 1e-12);
    EXPECT_EQ(layer.Change(Dark(10.0, 0.0), 5.0, 0.0, 0.1), 0.0);
    EXPECT_EQ(layer.Change(Dark(10.0, 0.081e-3), 0.0, 0.0, 0.1), 0.0);
    EXPECT_EQ(layer.Change(Dark(10.0, 0.0), 0.0, 0.0, 10.0), 0.0) << "too small to resolve";

    Splotch mixed = Dark(10.0, 0.0);
    mixed.otherChange = 0.10;
    mixed.split = 0.0;
    EXPECT_NEAR(layer.Change(mixed, -2.5, 0.0, 0.1), -0.04, 1e-12);
    EXPECT_NEAR(layer.Change(mixed, 2.5, 0.0, 0.1), 0.10, 1e-12);
    EXPECT_NEAR(layer.Change(mixed, 0.0, 0.0, 0.1), 0.03, 1e-12);
}

TEST(SplotchAlbedo, YoungerSplotchesCoverOlderOnes) {
    SplotchAlbedoOptions options;
    options.minDiameter = 2.0;
    options.darkShare = 0.0;
    options.brightShare = 1.0;
    const Albedo albedo = Gray(options);
    const RasterGrid grid{100, 100, GeoTransform::NorthUp(0.0, 100.0, 1.0)};
    const auto values = EvaluateAlbedo(albedo, grid, {0.5});
    int changed = 0;
    for (std::size_t row = 0; row < 100; ++row) {
        for (std::size_t col = 0; col < 100; ++col) {
            const float v = values[0](col, row);
            ASSERT_GE(v, 0.11f);
            ASSERT_LE(v, static_cast<float>(0.11 * 1.10) + 1e-6f) << "never stacked";
            changed += v > 0.1101f ? 1 : 0;
        }
    }
    EXPECT_GT(changed, 500);
}

TEST(SplotchAlbedo, GridsTilesAndPointsAgree) {
    SplotchAlbedoOptions options;
    options.minDiameter = 2.0;
    const Albedo albedo = Gray(options);
    const RasterGrid whole{96, 96, GeoTransform::NorthUp(-48.0, 48.0, 1.0)};
    const RasterGrid tile{32, 32, GeoTransform::NorthUp(-16.0, 16.0, 1.0)};
    const auto grid = EvaluateAlbedo(albedo, whole, {1.0, 1});
    const auto threaded = EvaluateAlbedo(albedo, whole, {1.0, 0});
    const auto part = EvaluateAlbedo(albedo, tile, {1.0});
    int changed = 0;
    for (std::size_t row = 0; row < 96; ++row) {
        for (std::size_t col = 0; col < 96; ++col) {
            ASSERT_EQ(grid[0](col, row), threaded[0](col, row));
            changed += grid[0](col, row) != 0.11f ? 1 : 0;
        }
    }
    EXPECT_GT(changed, 100);
    for (std::size_t row = 0; row < 32; ++row) {
        for (std::size_t col = 0; col < 32; ++col) {
            ASSERT_EQ(part[0](col, row), grid[0](col + 32, row + 32));
            if ((row * 32 + col) % 7 == 0) {
                const auto center = part[0].CellCenter(col, row);
                ASSERT_EQ(EvaluateAlbedoAt(albedo, center.x, center.y, {1.0})[0],
                          part[0](col, row));
            }
        }
    }
}

TEST(SplotchAlbedo, WorksOnASphereAndLeavesNoData) {
    SplotchAlbedoOptions options;
    options.minDiameter = 2.0;
    const Surface moon = Surface::Sphere(1737400.0);
    const Albedo albedo = Gray(options, moon);
    const double meter = 1.0 / moon.UnitLength();
    const RasterGrid grid{64, 64, GeoTransform::NorthUp(30.0, -10.0 + 64.0 * meter, meter)};
    const auto values = EvaluateAlbedo(albedo, grid, {1.0});
    int changed = 0;
    for (std::size_t row = 0; row < 64; ++row) {
        for (std::size_t col = 0; col < 64; ++col) {
            changed += values[0](col, row) != 0.11f ? 1 : 0;
        }
    }
    EXPECT_GT(changed, 50);
    const SplotchAlbedo layer(options);
    AlbedoSample value{NAN, 0.2f, 0.0f};
    layer.ApplyAt(value, 1, 0.0, 0.0, {1.0});
    EXPECT_TRUE(std::isnan(value[0]));
    std::vector<terraingen::Raster<float>> none;
    layer.Apply(none, {1.0});
    EXPECT_TRUE(none.empty());
}

TEST(SplotchAlbedoOptions, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateSplotchAlbedoOptions({}));
    const auto rejects = [](auto change) {
        SplotchAlbedoOptions options;
        change(options);
        EXPECT_THROW(terraingen::ValidateSplotchAlbedoOptions(options), std::invalid_argument);
        EXPECT_THROW(SplotchAlbedo{options}, std::invalid_argument);
    };
    rejects([](SplotchAlbedoOptions& o) { o.rateCoefficient = 0.0; });
    rejects([](SplotchAlbedoOptions& o) { o.rateExponent = -1.0; });
    rejects([](SplotchAlbedoOptions& o) { o.lifetimeMyr = 0.0; });
    rejects([](SplotchAlbedoOptions& o) { o.minDiameter = 0.0; });
    rejects([](SplotchAlbedoOptions& o) { o.maxDiameter = 1.0; });
    rejects([](SplotchAlbedoOptions& o) { o.darkShare = 1.1; });
    rejects([](SplotchAlbedoOptions& o) { o.brightShare = 0.2; });
    rejects(
        [](SplotchAlbedoOptions& o) { o.darkChange = {ValueDistribution::Kind::Constant, -1.0}; });
    rejects([](SplotchAlbedoOptions& o) {
        o.brightChange = {ValueDistribution::Kind::Normal, 0.1, 0.1};
    });
    rejects([](SplotchAlbedoOptions& o) { o.softness = 0.0; });
    rejects([](SplotchAlbedoOptions& o) { o.raggedness = 1.0; });
    rejects([](SplotchAlbedoOptions& o) { o.fade.endSamples = 1.0; });
}

namespace {

/// A plane tilted `degrees` toward +x.
struct Tilted final : terraingen::Ground {
    double grade = 0.0;
    explicit Tilted(double degrees) : grade(std::tan(degrees * terraingen::kPi / 180.0)) {}
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = static_cast<float>(grade * samples.Position(i).x);
        }
    }
};

}  // namespace

TEST(SplotchAlbedo, SteepSlopesCanFavorBrightSplotches) {
    SplotchAlbedoOptions options;
    options.minDiameter = 2.0;
    const Bounds2d region{0.0, 0.0, 150.0, 150.0};
    const Tilted steep(30.0);
    const Tilted gentle(5.0);
    const auto plain = SplotchAlbedo(options).Splotches(region, 0.5, &steep);
    options.steepBrightShare = 1.0;
    const SplotchAlbedo layer(options);
    const auto onSteep = layer.Splotches(region, 0.5, &steep);
    const auto onGentle = layer.Splotches(region, 0.5, &gentle);
    const auto flat = layer.Splotches(region, 0.5);
    ASSERT_EQ(onSteep.size(), plain.size());
    ASSERT_GT(onSteep.size(), 100u);
    int dark = 0;
    for (std::size_t n = 0; n < onSteep.size(); ++n) {
        ASSERT_DOUBLE_EQ(onSteep[n].change, 0.10);
        ASSERT_DOUBLE_EQ(onSteep[n].otherChange, 0.10);
        ASSERT_EQ(onGentle[n].change, plain[n].change) << "gentle slopes keep the shares";
        ASSERT_EQ(flat[n].change, plain[n].change) << "no ground, no slopes";
        dark += plain[n].change < 0.0 ? 1 : 0;
    }
    EXPECT_GT(dark, 0);
    options.steepBrightShare = 1.5;
    EXPECT_THROW(terraingen::ValidateSplotchAlbedoOptions(options), std::invalid_argument);
    options.steepBrightShare.reset();
    options.steepSlopeDegrees = 90.0;
    EXPECT_THROW(terraingen::ValidateSplotchAlbedoOptions(options), std::invalid_argument);

    options = {};
    options.minDiameter = 2.0;
    options.steepBrightShare = 1.0;
    SyntheticAlbedoOptions params;
    params.value = 0.11;
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params),
                        {std::make_shared<SplotchAlbedo>(options)}};
    const RasterGrid grid{64, 64, GeoTransform::NorthUp(0.0, 64.0, 1.0)};
    terraingen::LayerContext onSteepGround;
    onSteepGround.spacing = 1.0;
    onSteepGround.ground = &steep;
    const auto values = EvaluateAlbedo(albedo, grid, onSteepGround);
    for (std::size_t row = 0; row < 64; ++row) {
        for (std::size_t col = 0; col < 64; ++col) {
            ASSERT_GE(values[0](col, row), 0.11f) << "only bright splotches on the steep plane";
            if ((row * 64 + col) % 11 == 0) {
                const auto center = values[0].CellCenter(col, row);
                ASSERT_EQ(EvaluateAlbedoAt(albedo, center.x, center.y, onSteepGround)[0],
                          values[0](col, row));
            }
        }
    }
}

TEST(SplotchAlbedo, ReadsTheSlopeAcrossEachSplotch) {
    SplotchAlbedoOptions options;
    options.minDiameter = 2.0;
    options.steepBrightShare = 1.0;
    options.steepSlopeDegrees = 15.0;
    const SplotchAlbedo layer(options);
    const Bounds2d region{0.0, 0.0, 100.0, 100.0};
    const Tilted above(15.2);
    const Tilted below(14.8);
    const auto plain = layer.Splotches(region, 0.5);
    const auto steep = layer.Splotches(region, 0.5, &above);
    const auto gentle = layer.Splotches(region, 0.5, &below);
    ASSERT_GT(plain.size(), 10u);
    for (std::size_t n = 0; n < plain.size(); ++n) {
        EXPECT_DOUBLE_EQ(steep[n].change, 0.10);
        EXPECT_EQ(gentle[n].change, plain[n].change);
    }
}

TEST(SplotchSwarms, ScaleRobinsonsCraterByHousenAndHolsappsEjecta) {
    const terraingen::SplotchSwarms swarms;
    EXPECT_DOUBLE_EQ(swarms.Count(18.8), 248.0);
    EXPECT_NEAR(swarms.Count(37.6), 248.0 * std::pow(2.0, 3.615), 1e-9);
    EXPECT_NEAR(swarms.Coefficient(0.081) * std::pow(10.0, -3.06) * 1e6, 4.84e-6 * 81000.0, 1e-9)
        << "new craters of at least 10 m per km^2 within the lifetime";
}

TEST(SplotchAlbedo, SwarmsLandWhereRobinsonFoundThem) {
    SplotchAlbedoOptions options;
    options.swarms.enabled = true;
    const SplotchAlbedo layer(options);
    std::vector<double> ranges;
    int dark = 0;
    int wedges = 0;
    for (std::uint64_t id = 1; id <= 20; ++id) {
        const terraingen::Crater crater{0.0, 0.0, 18.8, 0.0, id, false};
        for (const Splotch& s : layer.Swarm(crater, 0.0)) {
            const double range = std::hypot(s.mark.x, s.mark.y);
            ASSERT_GE(range, 4300.0 - 1e-6);
            ASSERT_LE(range, 30000.0 + 1e-6);
            ASSERT_GE(s.mark.diameter, 2.0);
            ASSERT_LE(s.mark.diameter, 22.0);
            ranges.push_back(range);
            dark += s.change < 0.0 && s.otherChange < 0.0 ? 1 : 0;
            if (s.wedge) {
                ++wedges;
                EXPECT_NEAR(std::cos(s.heading - std::atan2(-s.mark.y, -s.mark.x)), 1.0, 1e-9)
                    << "pointing back to the crater";
            }
        }
    }
    const double n = static_cast<double>(ranges.size());
    EXPECT_NEAR(n, 20.0 * 248.0, 4.0 * std::sqrt(20.0 * 248.0));
    std::sort(ranges.begin(), ranges.end());
    EXPECT_NEAR(ranges[ranges.size() / 2], 8700.0, 500.0) << "Robinson's median distance";
    double mean = 0.0;
    for (const double r : ranges) {
        mean += r / n;
    }
    EXPECT_NEAR(mean, 10300.0, 1200.0) << "Robinson's mean distance";
    EXPECT_NEAR(dark / n, 239.0 / 248.0, 0.015);
    EXPECT_NEAR(wedges / n, 0.2, 0.02);
}

TEST(SplotchAlbedo, WedgesNarrowTowardTheirCrater) {
    const SplotchAlbedo layer;
    Splotch wedge;
    wedge.mark = {0.0, 0.0, 10.0, 0.0, 3u, false};
    wedge.change = -0.04;
    wedge.otherChange = -0.04;
    wedge.wedge = true;
    wedge.elongation = 2.0;
    wedge.heading = 0.0;
    EXPECT_LT(layer.Change(wedge, -4.0, 0.0, 0.1), 0.0) << "the long axis reaches 7 m";
    EXPECT_LT(layer.Change(wedge, -3.0, 1.8, 0.1), 0.0) << "broad at the base";
    EXPECT_EQ(layer.Change(wedge, 3.0, 1.8, 0.1), 0.0) << "narrow toward the crater";
}

TEST(SplotchAlbedo, SwarmsKeepGridsAndPointsInStep) {
    SplotchAlbedoOptions options;
    options.minDiameter = 4.0;
    options.swarms.enabled = true;
    options.swarms.craterRate = 1.5e-3;
    options.swarms.minRange = 30.0;
    options.swarms.maxRange = 300.0;
    options.swarms.referenceCount = 20.0;
    const Albedo albedo = Gray(options);
    const RasterGrid grid{48, 48, GeoTransform::NorthUp(0.0, 192.0, 4.0)};
    const auto values = EvaluateAlbedo(albedo, grid, {4.0});
    const auto swarmed = SplotchAlbedo(options).Splotches({0.0, 0.0, 192.0, 192.0}, 4.0).size();
    SplotchAlbedoOptions plain = options;
    plain.swarms.enabled = false;
    EXPECT_GT(swarmed, SplotchAlbedo(plain).Splotches({0.0, 0.0, 192.0, 192.0}, 4.0).size());
    for (std::size_t row = 0; row < 48; row += 3) {
        for (std::size_t col = 0; col < 48; col += 3) {
            const auto center = values[0].CellCenter(col, row);
            ASSERT_EQ(EvaluateAlbedoAt(albedo, center.x, center.y, {4.0})[0], values[0](col, row));
        }
    }
}

TEST(SplotchSwarms, RejectsInvalidSettings) {
    const auto rejects = [](auto change) {
        SplotchAlbedoOptions options;
        change(options.swarms);
        EXPECT_THROW(terraingen::ValidateSplotchAlbedoOptions(options), std::invalid_argument);
    };
    using Swarms = terraingen::SplotchSwarms;
    rejects([](Swarms& s) { s.craterRate = 0.0; });
    rejects([](Swarms& s) { s.maxCraterDiameter = 5.0; });
    rejects([](Swarms& s) { s.maxRange = 1000.0; });
    rejects([](Swarms& s) { s.mu = 1.0; });
    rejects([](Swarms& s) { s.splotchDiameter = {ValueDistribution::Kind::LogNormal, 6.0, 0.6}; });
    rejects([](Swarms& s) { s.darkChange = {ValueDistribution::Kind::Constant, -1.5}; });
    rejects([](Swarms& s) { s.brightShare = 0.5; });
    rejects([](Swarms& s) { s.wedgeShare = 2.0; });
    rejects([](Swarms& s) { s.wedgeElongation = 0.5; });
}
