#include "terraingen/layers/sand_shadow.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::Boulder;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::SandShadowLayer;
using terraingen::SandShadowOptions;

namespace {

constexpr double kDegree = terraingen::kPi / 180.0;

/// Sparse boulders 1 to 2 m wide.
std::shared_ptr<const BoulderLayer> Sparse() {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 2.0;
    return std::make_shared<BoulderLayer>(options);
}

/// A boulder of `layer` with no other within `clear` metres.
Boulder Isolated(const BoulderLayer& layer, double clear) {
    const auto boulders = layer.Boulders({-150.0, -150.0, 150.0, 150.0});
    for (const Boulder& boulder : boulders) {
        bool alone = true;
        for (const Boulder& other : boulders) {
            if (other.id != boulder.id &&
                std::hypot(other.x - boulder.x, other.y - boulder.y) < clear) {
                alone = false;
            }
        }
        if (alone) {
            return boulder;
        }
    }
    ADD_FAILURE() << "no isolated boulder";
    return {};
}

}  // namespace

TEST(SandShadowLayer, LaysATaperingTailBehindEachBoulder) {
    const auto boulders = Sparse();
    const SandShadowOptions options;
    const SandShadowLayer layer(boulders, options);
    const Boulder b = Isolated(*boulders, 12.0);
    const double w = b.diameter;
    const double edge = b.x + 0.5 * w;
    const double length = options.lengthRatio * w;
    const double slope = std::tan(options.flankAngle);
    const double crest = std::min(0.5 * w * slope, b.ExposedHeight());
    EXPECT_NEAR(layer.ThicknessAt(edge + 1e-6, b.y), crest, 1e-5);
    EXPECT_NEAR(layer.ThicknessAt(edge + 0.5 * length, b.y), 0.5 * crest, 1e-9);
    EXPECT_EQ(layer.ThicknessAt(edge + length + 1e-6, b.y), 0.0);
    EXPECT_EQ(layer.ThicknessAt(b.x - 0.5 * w - 0.2, b.y), 0.0) << "nothing upwind";
    EXPECT_EQ(layer.ThicknessAt(edge + 0.01, b.y + 0.5 * w + 0.01), 0.0) << "nothing beside";
    const double flank = layer.ThicknessAt(edge + 0.5 * length, b.y + 0.1 * w);
    EXPECT_NEAR(flank, 0.3 * crest, 1e-9) << "flanks fall straight from the crest line";
}

TEST(SandShadowLayer, NoHigherThanTheBoulderStands) {
    const auto boulders = Sparse();
    SandShadowOptions options;
    options.flankAngle = 80.0 * kDegree;
    const SandShadowLayer layer(boulders, options);
    const Boulder b = Isolated(*boulders, 12.0);
    const double edge = b.x + 0.5 * b.diameter;
    EXPECT_NEAR(layer.ThicknessAt(edge + 1e-9, b.y), b.ExposedHeight(), 1e-6);
    EXPECT_NEAR(layer.ThicknessAt(edge + 0.5 * options.lengthRatio * b.diameter, b.y),
                0.5 * b.ExposedHeight(), 1e-9);
}

TEST(SandShadowLayer, TailsPointDownwind) {
    const auto boulders = Sparse();
    SandShadowOptions options;
    options.windAzimuth = 0.5 * terraingen::kPi;
    const SandShadowLayer layer(boulders, options);
    const Boulder b = Isolated(*boulders, 12.0);
    EXPECT_GT(layer.ThicknessAt(b.x, b.y + 0.5 * b.diameter + 0.05), 0.0);
    EXPECT_EQ(layer.ThicknessAt(b.x + 0.5 * b.diameter + 0.05, b.y), 0.0);
}

TEST(SandShadowLayer, SkipsSmallAndUnresolvedBoulders) {
    const auto boulders = Sparse();
    const Boulder b = Isolated(*boulders, 12.0);
    const double x = b.x + 0.5 * b.diameter + 0.05;
    SandShadowOptions options;
    options.minDiameter = 2.0;
    EXPECT_EQ(SandShadowLayer(boulders, options).ThicknessAt(x, b.y), 0.0);
    const SandShadowLayer layer(boulders, SandShadowOptions{});
    EXPECT_GT(layer.ThicknessAt(x, b.y, 0.05), 0.0);
    EXPECT_EQ(layer.ThicknessAt(x, b.y, 5.0), 0.0) << "too small for the spacing";
    SandShadowOptions strict;
    strict.fade = {20.0, 40.0, terraingen::BlendShape::Smoothstep};
    EXPECT_EQ(SandShadowLayer(boulders, strict).ThicknessAt(x, b.y, 0.1), 0.0)
        << "a stricter fade than the boulders'";
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), x, b.y, {0.05})));
}

TEST(SandShadowLayer, GridsAndPointsAgreeAndCoverIsSand) {
    const auto boulders = Sparse();
    const SandShadowLayer layer(boulders, SandShadowOptions{});
    const Boulder b = Isolated(*boulders, 12.0);
    const terraingen::RasterGrid grid{48, 32,
                                      terraingen::GeoTransform::NorthUp(b.x - 4.0, b.y + 3.2, 0.2)};
    terraingen::Raster<float> heights = grid.MakeRaster();
    layer.Apply(heights, {0.2, 2});
    double deepest = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {0.2}), heights(col, row));
            deepest = std::max(deepest, static_cast<double>(heights(col, row)));
        }
    }
    EXPECT_GT(deepest, 0.1);
    EXPECT_EQ(layer.Material(), terraingen::MaterialClass::Sand);
    EXPECT_EQ(layer.MaterialCoverAt(b.x + 0.5 * b.diameter + 0.01, b.y, 0.05, nullptr), 1.0);
    EXPECT_EQ(layer.MaterialCoverAt(b.x - b.diameter, b.y, 0.05, nullptr), 0.0);
    EXPECT_EQ(layer.Options(), SandShadowOptions{});
}

TEST(SandShadowLayer, FollowsTheBoulderFramesOnABody) {
    const terraingen::Surface mars = terraingen::Surface::Sphere(3389500.0);
    BoulderLayerOptions options;
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 2.0;
    const auto boulders = std::make_shared<BoulderLayer>(options, nullptr, mars);
    const SandShadowLayer layer(boulders, SandShadowOptions{}, mars);
    const auto placed = boulders->Boulders({137.4, -4.6, 137.402, -4.598});
    ASSERT_FALSE(placed.empty());
    const Boulder& b = placed.front();
    const terraingen::Vec2d behind = mars.Locate({b.x, b.y}, {0.5 * b.diameter + 0.01, 0.0});
    EXPECT_GT(layer.ThicknessAt(behind.x, behind.y), 0.0);
}

TEST(SandShadowLayer, RejectsBadOptions) {
    const auto boulders = Sparse();
    EXPECT_THROW(SandShadowLayer(nullptr, SandShadowOptions{}), std::invalid_argument);
    const auto rejects = [&](auto change) {
        SandShadowOptions options;
        change(options);
        EXPECT_THROW(SandShadowLayer(boulders, options), std::invalid_argument);
    };
    rejects([](SandShadowOptions& o) { o.windAzimuth = NAN; });
    rejects([](SandShadowOptions& o) { o.lengthRatio = 0.0; });
    rejects([](SandShadowOptions& o) { o.flankAngle = 0.0; });
    rejects([](SandShadowOptions& o) { o.flankAngle = 0.5 * terraingen::kPi; });
    rejects([](SandShadowOptions& o) { o.minDiameter = -1.0; });
    rejects([](SandShadowOptions& o) { o.coverThickness = 0.0; });
    rejects([](SandShadowOptions& o) { o.fade.startSamples = -1.0; });
}

TEST(SandShadowLayer, GivesTheSameCoverOverAGridAsAtItsPoints) {
    const auto boulders = Sparse();
    SandShadowOptions options;
    options.minDiameter = 1.3;
    const SandShadowLayer layer(boulders, options);
    const terraingen::RasterGrid grid{240, 240,
                                      terraingen::GeoTransform::NorthUp(-60.0, 60.0, 0.5)};
    const terraingen::Raster<double> cover = layer.MaterialCoverOver(grid, 0.5, nullptr, 3);
    double sand = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto centre = cover.CellCenter(col, row);
            ASSERT_EQ(cover(col, row), layer.MaterialCoverAt(centre.x, centre.y, 0.5, nullptr));
            sand += cover(col, row);
        }
    }
    EXPECT_GT(sand, 0.0);
}
