// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/strata.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::StrataLayer;
using terraingen::StrataOptions;

namespace {

constexpr double kDegree = terraingen::kPi / 180.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// Ground with a constant height.
class Level final : public terraingen::Ground {
  public:
    explicit Level(double height) : height_(height) {}

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = static_cast<float>(height_);
        }
    }

  private:
    double height_;
};

/// The terraced height of ground `height` meters high at (x, y).
double Terraced(const StrataLayer& layer, double height, double x, double y) {
    return static_cast<double>(layer.ApplyAt(static_cast<float>(height), x, y, {1.0}));
}

/// The heights of the bed boundaries along a vertical profile: where the terracing, which lowers
/// the ground everywhere but at a boundary, peaks back to the ground.
std::vector<double> Boundaries(const StrataLayer& layer, double from, double to, double step) {
    const auto lowered = [&](double h) {
        return Terraced(layer, h, 0.3, 0.7) - static_cast<double>(static_cast<float>(h));
    };
    std::vector<double> boundaries;
    double before = lowered(from);
    double here = lowered(from + step);
    for (double h = from + step; h + step < to; h += step) {
        const double after = lowered(h + step);
        if (here > -0.01 && here >= before && here > after) {
            boundaries.push_back(h);
        }
        before = here;
        here = after;
    }
    return boundaries;
}

}  // namespace

TEST(StrataLayer, TerracingIsContinuousRisingAndKeepsBedBoundaries) {
    const StrataLayer layer(StrataOptions{});
    double previous = Terraced(layer, -20.0, 0.3, 0.7);
    double jump = 0.0;
    for (double h = -20.0 + 1e-3; h < 20.0; h += 1e-3) {
        const double terraced = Terraced(layer, h, 0.3, 0.7);
        EXPECT_GE(terraced, previous - 1e-5);
        jump = std::max(jump, terraced - previous);
        previous = terraced;
    }
    EXPECT_LT(jump, 0.02) << "no steps where one bed meets the next";
    for (const double boundary : Boundaries(layer, -20.0, 20.0, 1e-3)) {
        EXPECT_NEAR(Terraced(layer, boundary, 0.3, 0.7), boundary, 2e-3);
    }
}

TEST(StrataLayer, BenchesAreFlatAndCliffsSteepAtTheTopOfEachBed) {
    StrataOptions options;
    options.thicknessVariation = 0.0;
    options.sharpness = 4.0;
    const StrataLayer layer(options);
    const double d = 1e-3;
    const auto rate = [&](double h) {
        return (Terraced(layer, h + d, 0.0, 0.0) - Terraced(layer, h - d, 0.0, 0.0)) / (2.0 * d);
    };
    EXPECT_LT(rate(4.2), 0.01) << "the bench low in a bed";
    EXPECT_GT(rate(5.9), 3.0) << "the cliff at its top";
    EXPECT_NEAR(Terraced(layer, 5.0, 0.0, 0.0), 4.0 + 2.0 * std::pow(0.5, 4.0), 1e-6);
    EXPECT_NEAR(Terraced(layer, -1.0, 0.0, 0.0), -2.0 + 2.0 * std::pow(0.5, 4.0), 1e-6);
}
TEST(StrataLayer, SharpnessOneAndStrengthZeroLeaveTheGround) {
    StrataOptions options;
    options.sharpness = 1.0;
    const StrataLayer flat(options);
    options.sharpness = 4.0;
    options.strength = 0.0;
    const StrataLayer off(options);
    options.strength = 0.5;
    const StrataLayer half(options);
    options.strength = 1.0;
    const StrataLayer full(options);
    for (double h = -5.0; h < 5.0; h += 0.37) {
        const double ground = static_cast<double>(static_cast<float>(h));
        EXPECT_NEAR(Terraced(flat, h, 1.0, 2.0), ground, 1e-6);
        EXPECT_EQ(Terraced(off, h, 1.0, 2.0), ground);
        EXPECT_NEAR(Terraced(half, h, 1.0, 2.0), 0.5 * (ground + Terraced(full, h, 1.0, 2.0)),
                    1e-6);
    }
}

TEST(StrataLayer, BedThicknessesVaryWithinTheirBounds) {
    StrataOptions options;
    options.bedThickness = 1.5;
    options.thicknessVariation = 0.6;
    const StrataLayer layer(options);
    const std::vector<double> boundaries = Boundaries(layer, -60.0, 60.0, 1e-3);
    ASSERT_GT(boundaries.size(), 70u);
    double thinnest = 1e9;
    double thickest = 0.0;
    for (std::size_t k = 1; k < boundaries.size(); ++k) {
        const double thickness = boundaries[k] - boundaries[k - 1];
        thinnest = std::min(thinnest, thickness);
        thickest = std::max(thickest, thickness);
    }
    EXPECT_GE(thinnest, 1.5 * (1.0 - 0.6) - 2e-3);
    EXPECT_LE(thickest, 1.5 * (1.0 + 0.6) + 2e-3);
    EXPECT_GT(thickest - thinnest, 0.5) << "the beds are not all one thickness";
    options.seed = 2;
    EXPECT_NE(Boundaries(StrataLayer(options), -60.0, 60.0, 1e-3), boundaries);
}

TEST(StrataLayer, BedsDipTowardTheirAzimuth) {
    StrataOptions options;
    options.dipDegrees = 10.0;
    options.dipAzimuthDegrees = (0.5 * terraingen::kPi) * terraingen::kRadian;
    const StrataLayer layer(options);
    const double drop = std::tan(options.dipDegrees * terraingen::kDegree) * 7.0;
    const double here = Terraced(layer, 3.3, 2.0, 1.0) - 3.3;
    EXPECT_NEAR(here, Terraced(layer, 3.3 - drop, 2.0, 8.0) - (3.3 - drop), 1e-5);
    EXPECT_NEAR(here, Terraced(layer, 3.3, 9.0, 1.0) - 3.3, 1e-6) << "level along the strike";
}

TEST(StrataLayer, CliffsAreBedrockAndBenchesAreNot) {
    StrataOptions options;
    options.thicknessVariation = 0.0;
    options.cliffShare = 0.3;
    const StrataLayer layer(options);
    EXPECT_EQ(layer.Material(), terraingen::MaterialClass::Bedrock);
    const Level cliff(1.9);
    const Level bench(0.4);
    const Level brink(1.4);
    const Level none(std::nan(""));
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &cliff}), 1.0);
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &bench}), 0.0);
    EXPECT_NEAR(layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &brink}), 0.5, 1e-6);
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &none}), 0.0);
    EXPECT_THROW(layer.MaterialCoverAt(0.0, 0.0, {0.1}), std::invalid_argument);
    options.strength = 0.0;
    EXPECT_EQ(StrataLayer(options).MaterialCoverAt(0.0, 0.0, {0.1, 1, &cliff}), 0.0);
}

TEST(StrataLayer, GridsAndPointsAgree) {
    StrataOptions options;
    options.dipDegrees = 3.0;
    options.dipAzimuthDegrees = 1.1 * terraingen::kRadian;
    const StrataLayer layer(options);
    const terraingen::RasterGrid grid{40, 30, terraingen::GeoTransform::NorthUp(-10.0, 20.0, 0.5)};
    terraingen::Raster<float> heights = grid.MakeRaster();
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            heights(col, row) = static_cast<float>(0.3 * static_cast<double>(col + row));
        }
    }
    const terraingen::Raster<float> before = heights;
    layer.Apply(heights, {0.5, 2});
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(before(col, row), center.x, center.y, {0.5}),
                      heights(col, row));
        }
    }
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.5})));
}

TEST(StrataLayer, DipsInTheAnchorsFrameOnABody) {
    const terraingen::Surface mars = terraingen::Surface::Sphere(3389500.0);
    StrataOptions options;
    options.dipDegrees = 5.0;
    options.anchor = {137.4, -4.6};
    const StrataLayer layer(options);
    const terraingen::Vec2d point{137.4003, -4.5998};
    const terraingen::Vec2d offset = mars.Offset(options.anchor, point);
    terraingen::LayerContext onMars{0.01};
    onMars.surface = &mars;
    EXPECT_NEAR(static_cast<double>(layer.ApplyAt(1.3f, point.x, point.y, onMars)),
                Terraced(layer, 1.3, offset.x, offset.y), 1e-6);
    EXPECT_EQ(layer.ApplyAt(2.0f, -42.6, 4.6, onMars), 2.0f);
}

TEST(StrataLayer, ReseedsItsBeds) {
    StrataOptions options;
    options.thicknessVariation = 0.5;
    const StrataLayer layer(options);
    const terraingen::LayerPtr a = layer.Reseeded(1);
    const terraingen::LayerPtr c = layer.Reseeded(2);
    double differ = 0.0;
    for (double h = -20.0; h < 20.0; h += 0.37) {
        const float ta = a->ApplyAt(static_cast<float>(h), 0.0, 0.0, {1.0});
        EXPECT_EQ(ta, layer.Reseeded(1)->ApplyAt(static_cast<float>(h), 0.0, 0.0, {1.0}));
        differ +=
            std::fabs(static_cast<double>(ta - c->ApplyAt(static_cast<float>(h), 0.0, 0.0, {1.0})));
    }
    EXPECT_GT(differ, 0.0);
}

TEST(StrataLayer, RejectsBadOptions) {
    const auto rejects = [](auto change) {
        StrataOptions options;
        change(options);
        EXPECT_THROW(StrataLayer{options}, std::invalid_argument);
    };
    rejects([](StrataOptions& o) { o.bedThickness = 0.0; });
    rejects([](StrataOptions& o) { o.thicknessVariation = 1.0; });
    rejects([](StrataOptions& o) { o.thicknessVariation = -0.1; });
    rejects([](StrataOptions& o) { o.dipDegrees = (0.5 * terraingen::kPi) * terraingen::kRadian; });
    rejects([](StrataOptions& o) { o.sharpness = 0.9; });
    rejects([](StrataOptions& o) { o.sharpness = std::numeric_limits<double>::infinity(); });
    rejects([](StrataOptions& o) { o.strength = 1.1; });
    rejects([](StrataOptions& o) { o.cliffShare = 0.0; });
    rejects([](StrataOptions& o) { o.dipAzimuthDegrees = (kNaN)*terraingen::kRadian; });
    rejects([](StrataOptions& o) { o.anchor = {kNaN, 0.0}; });
    rejects([](StrataOptions& o) { o.hardShare = 1.5; });
    rejects([](StrataOptions& o) { o.softSharpness = 0.5; });
}

TEST(StrataLayer, SoftBedsSlopeWithoutCliffsOrBedrock) {
    StrataOptions options;
    options.thicknessVariation = 0.0;
    options.hardShare = 0.5;
    options.softSharpness = 1.0;
    const StrataLayer layer(options);
    int hard = 0;
    int soft = 0;
    for (int k = 0; k < 400; ++k) {
        const double mid = (static_cast<double>(k) + 0.6) * options.bedThickness;
        const double terraced = Terraced(layer, mid, 0.0, 0.0);
        const Level ground(mid + 0.3 * options.bedThickness);
        const double cover = layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &ground});
        if (terraced == static_cast<double>(static_cast<float>(mid))) {
            ++soft;
            EXPECT_EQ(cover, 0.0) << "no bedrock on a soft bed";
        } else {
            ++hard;
            EXPECT_EQ(cover, 1.0) << "bedrock on a hard bed's cliff";
        }
    }
    EXPECT_NEAR(static_cast<double>(soft) / 400.0, 0.5, 0.08);
    EXPECT_GT(hard, 0);
    options.hardShare = 1.0;
    const StrataLayer allHard(options);
    EXPECT_NE(Terraced(allHard, 0.6 * options.bedThickness, 0.0, 0.0), 0.6 * options.bedThickness);
    options.hardShare = 0.0;
    options.softSharpness = 2.0;
    const StrataLayer allSoft(options);
    EXPECT_NEAR(Terraced(allSoft, 0.5 * options.bedThickness, 0.0, 0.0),
                0.25 * options.bedThickness, 1e-6)
        << "soft beds cut at their own sharpness";
}
