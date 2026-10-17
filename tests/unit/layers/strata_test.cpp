#include "terraingen/layers/strata.h"

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

using terraingen::StrataLayer;
using terraingen::StrataOptions;

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

/// Ground with a constant height.
class Level final : public terraingen::Ground {
  public:
    explicit Level(double height) : height_(height) {}
    float HeightAt(double, double, double) const override {
        return static_cast<float>(height_);
    }

  private:
    double height_;
};

/// The heights where the bed position wraps from near 1 back to near 0 along a vertical profile.
std::vector<double> Boundaries(const StrataLayer& layer, double from, double to, double step) {
    std::vector<double> boundaries;
    double previous = layer.BedPositionAt(from, 0.3, 0.7);
    for (double h = from + step; h < to; h += step) {
        const double u = layer.BedPositionAt(h, 0.3, 0.7);
        if (u < previous) {
            boundaries.push_back(h);
        }
        previous = u;
    }
    return boundaries;
}

}  // namespace

TEST(StrataLayer, TerracingIsContinuousRisingAndKeepsBedBoundaries) {
    const StrataLayer layer(StrataOptions{});
    double previous = layer.TerracedAt(-20.0, 0.3, 0.7);
    double jump = 0.0;
    for (double h = -20.0 + 1e-3; h < 20.0; h += 1e-3) {
        const double terraced = layer.TerracedAt(h, 0.3, 0.7);
        EXPECT_GE(terraced, previous - 1e-12);
        jump = std::max(jump, terraced - previous);
        previous = terraced;
    }
    EXPECT_LT(jump, 0.02) << "no steps where one bed meets the next";
    for (const double boundary : Boundaries(layer, -20.0, 20.0, 1e-3)) {
        EXPECT_NEAR(layer.TerracedAt(boundary, 0.3, 0.7), boundary, 2e-3);
    }
}

TEST(StrataLayer, BenchesAreFlatAndCliffsSteepAtTheTopOfEachBed) {
    StrataOptions options;
    options.thicknessVariation = 0.0;
    options.sharpness = 4.0;
    const StrataLayer layer(options);
    const double d = 1e-4;
    const auto rate = [&](double h) {
        return (layer.TerracedAt(h + d, 0.0, 0.0) - layer.TerracedAt(h - d, 0.0, 0.0)) / (2.0 * d);
    };
    EXPECT_LT(rate(4.2), 0.01) << "the bench low in a bed";
    EXPECT_GT(rate(5.9), 3.0) << "the cliff at its top";
    EXPECT_NEAR(layer.TerracedAt(5.0, 0.0, 0.0), 4.0 + 2.0 * std::pow(0.5, 4.0), 1e-12);
    EXPECT_NEAR(layer.BedPositionAt(5.0, 0.0, 0.0), 0.5, 1e-12);
    EXPECT_NEAR(layer.BedPositionAt(-1.0, 0.0, 0.0), 0.5, 1e-12);
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
        EXPECT_NEAR(flat.TerracedAt(h, 1.0, 2.0), h, 1e-12);
        EXPECT_EQ(off.TerracedAt(h, 1.0, 2.0), h);
        EXPECT_NEAR(half.TerracedAt(h, 1.0, 2.0), 0.5 * (h + full.TerracedAt(h, 1.0, 2.0)), 1e-12);
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
    options.dip = 10.0 * kDegree;
    options.dipAzimuth = 0.5 * std::numbers::pi;
    const StrataLayer layer(options);
    const double drop = std::tan(options.dip) * 7.0;
    EXPECT_NEAR(layer.BedPositionAt(3.3, 2.0, 1.0), layer.BedPositionAt(3.3 - drop, 2.0, 8.0),
                1e-9);
    EXPECT_NEAR(layer.BedPositionAt(3.3, 2.0, 1.0), layer.BedPositionAt(3.3, 9.0, 1.0), 1e-12)
        << "level along the strike";
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
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, 0.1, &cliff), 1.0);
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, 0.1, &bench), 0.0);
    EXPECT_NEAR(layer.MaterialCoverAt(0.0, 0.0, 0.1, &brink), 0.5, 1e-6);
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, 0.1, &none), 0.0);
    EXPECT_THROW(layer.MaterialCoverAt(0.0, 0.0, 0.1, nullptr), std::invalid_argument);
    options.strength = 0.0;
    EXPECT_EQ(StrataLayer(options).MaterialCoverAt(0.0, 0.0, 0.1, &cliff), 0.0);
}

TEST(StrataLayer, GridsAndPointsAgree) {
    StrataOptions options;
    options.dip = 3.0 * kDegree;
    options.dipAzimuth = 1.1;
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
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(before(col, row), centre.x, centre.y, {0.5}),
                      heights(col, row));
        }
    }
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.5})));
}

TEST(StrataLayer, DipsInTheAnchorsFrameOnABody) {
    const terraingen::Surface mars = terraingen::Surface::Sphere(3389500.0);
    StrataOptions options;
    options.dip = 5.0 * kDegree;
    options.anchor = {137.4, -4.6};
    const StrataLayer body(options, mars);
    const StrataLayer plane(options);
    const terraingen::Vec2d point{137.4003, -4.5998};
    const terraingen::Vec2d offset = mars.Offset(options.anchor, point);
    EXPECT_NEAR(body.TerracedAt(1.3, point.x, point.y), plane.TerracedAt(1.3, offset.x, offset.y),
                1e-9);
    EXPECT_EQ(body.TerracedAt(1.3, -42.6, 4.6), 1.3);
    EXPECT_EQ(body.BedPositionAt(1.3, -42.6, 4.6), 0.0);
    EXPECT_EQ(body.ApplyAt(2.0f, -42.6, 4.6, {0.01}), 2.0f);
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
    rejects([](StrataOptions& o) { o.dip = 0.5 * std::numbers::pi; });
    rejects([](StrataOptions& o) { o.sharpness = 0.9; });
    rejects([](StrataOptions& o) { o.sharpness = INFINITY; });
    rejects([](StrataOptions& o) { o.strength = 1.1; });
    rejects([](StrataOptions& o) { o.cliffShare = 0.0; });
    rejects([](StrataOptions& o) { o.dipAzimuth = NAN; });
    rejects([](StrataOptions& o) { o.anchor = {NAN, 0.0}; });
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
        const double terraced = layer.TerracedAt(mid, 0.0, 0.0);
        const Level ground(mid + 0.3 * options.bedThickness);
        const double cover = layer.MaterialCoverAt(0.0, 0.0, 0.1, &ground);
        if (std::fabs(terraced - mid) < 1e-12) {
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
    EXPECT_NE(allHard.TerracedAt(0.6 * options.bedThickness, 0.0, 0.0), 0.6 * options.bedThickness);
    options.hardShare = 0.0;
    options.softSharpness = 2.0;
    const StrataLayer allSoft(options);
    EXPECT_NEAR(allSoft.TerracedAt(0.5 * options.bedThickness, 0.0, 0.0),
                0.25 * options.bedThickness, 1e-12)
        << "soft beds cut at their own sharpness";
}
