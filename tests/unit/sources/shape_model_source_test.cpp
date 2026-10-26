// SPDX-License-Identifier: BSD-3-Clause

#include "support/source_agreement.h"
#include "terraingen/sources/shape_model_source.h"

#include "terraingen/sources/diamond_square.h"
#include "terraingen/util/radial_shape.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::DiamondSquareOptions;
using terraingen::DiamondSquareUpsampler;
using terraingen::GeoTransform;
using terraingen::KernelUpsampler;
using terraingen::RadialShape;
using terraingen::RasterGrid;
using terraingen::ResampleMethod;
using terraingen::ShapeModelSource;
using terraingen::Surface;
using terraingen::Vec2d;
using terraingen::Vec3d;

namespace {

/// A smooth lumpy radius in meters along unit direction `u`.
double Lumpy(const Vec3d& u) {
    return 300.0 + 60.0 * u.x * u.x + 25.0 * std::sin(3.0 * u.y) * u.z;
}

/// A body of `q` intervals per face edge sampled from `Lumpy`.
Surface LumpyBody(std::size_t q) {
    return Surface::ShapeModel(std::make_shared<RadialShape>(RadialShape::Sample(q, Lumpy)));
}

/// The longitude and latitude in degrees of unit direction `u`.
Vec2d LonLat(const Vec3d& u) {
    constexpr double kDegrees = 180.0 / terraingen::kPi;
    return {std::atan2(u.y, u.x) * kDegrees, std::asin(u.z) * kDegrees};
}

}  // namespace

TEST(ShapeModelSource, NeedsAShapeModelBody) {
    EXPECT_THROW(ShapeModelSource{Surface::Sphere(100.0)}, std::invalid_argument);
    EXPECT_THROW(ShapeModelSource{Surface::Plane()}, std::invalid_argument);
}

TEST(ShapeModelSource, CoversTheWholeBodyAtTheLabelSpacing) {
    const ShapeModelSource source(LumpyBody(16));
    EXPECT_FALSE(terraingen::BoundedGrid(source).has_value());
    EXPECT_DOUBLE_EQ(source.Grid().PixelSize(), 90.0 / 16.0);
    EXPECT_DOUBLE_EQ(source.ResolutionAt(10.0, 20.0), 90.0 / 16.0);
    EXPECT_DOUBLE_EQ(source.PostSpacingAt(10.0, 20.0), 90.0 / 16.0);
    EXPECT_TRUE(source.Keys().Empty());
    for (const auto& upsampler : source.UpsamplersInUse()) {
        EXPECT_EQ(upsampler, terraingen::DefaultUpsampler());
    }
    EXPECT_TRUE(source.Body().IsBody());
}

TEST(ShapeModelSource, IsZeroAtEveryLabel) {
    const std::size_t q = 8;
    const ShapeModelSource source(LumpyBody(q));
    terraingen::ForEachIcqLabel(
        q, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t) {
            const Vec2d at = LonLat(RadialShape::LabelDirection(q, i, j, face));
            EXPECT_NEAR(source.HeightAt(at.x, at.y, 90.0 / q), 0.0f, 1e-3)
                << i << ", " << j << ", " << face;
        });
}

TEST(ShapeModelSource, CubicFollowsASmoothShapeCloserThanBilinear) {
    const Surface body = LumpyBody(12);
    const ShapeModelSource source(body);
    double bilinear = 0.0;
    double cubic = 0.0;
    for (double lon = -175.0; lon < 180.0; lon += 7.3) {
        for (double lat = -80.0; lat < 85.0; lat += 6.1) {
            const double truth = Lumpy(terraingen::UnitVector(lon, lat));
            const double shape = body.RadiusAt({lon, lat});
            bilinear += std::abs(shape - truth);
            cubic += std::abs(shape + static_cast<double>(source.HeightAt(lon, lat, 0.1)) - truth);
        }
    }
    EXPECT_LT(cubic, 0.5 * bilinear);
}

TEST(ShapeModelSource, IsContinuousAcrossFaceEdges) {
    const Surface body = LumpyBody(16);
    DiamondSquareOptions rough;
    rough.fractalDimension = 2.4;
    rough.scale = 2.0;
    for (const auto& upsampler : std::vector<std::shared_ptr<const terraingen::Upsampler>>{
             nullptr, std::make_shared<KernelUpsampler>(ResampleMethod::Lanczos3),
             std::make_shared<DiamondSquareUpsampler>(rough)}) {
        const ShapeModelSource source(body, upsampler);
        // Face edges at 45 E on the equator (between +x and +y) and 45 N at 0 E (+x and +z).
        for (const Vec2d edge : {Vec2d{45.0, 10.0}, Vec2d{0.0, 45.0}, Vec2d{-30.0, -45.0}}) {
            const double step = 1e-6;
            const float below = source.HeightAt(edge.x - step, edge.y - step, 0.05);
            const float above = source.HeightAt(edge.x + step, edge.y + step, 0.05);
            EXPECT_NEAR(below, above, 1e-3) << edge.x << ", " << edge.y;
        }
    }
}

TEST(ShapeModelSource, DiamondSquareAddsDetailThatDiffersByFace) {
    DiamondSquareOptions rough;
    rough.fractalDimension = 2.5;
    rough.scale = 1.0;
    const ShapeModelSource source(LumpyBody(16), std::make_shared<DiamondSquareUpsampler>(rough));
    const auto patch = [&](double lon, double lat) {
        std::vector<float> heights;
        for (int n = 0; n < 16; ++n) {
            heights.push_back(source.HeightAt(lon + 0.37 * n, lat + 0.11 * n, 0.05));
        }
        return heights;
    };
    const std::vector<float> east = patch(5.0, 5.0);    // face 5, +x
    const std::vector<float> north = patch(95.0, 5.0);  // face 4, +y
    double spread = 0.0;
    double difference = 0.0;
    for (std::size_t n = 0; n < east.size(); ++n) {
        spread += std::abs(static_cast<double>(east[n]));
        difference += std::abs(static_cast<double>(east[n] - north[n]));
    }
    EXPECT_GT(spread, 0.1);
    EXPECT_GT(difference, 0.1);
}

TEST(ShapeModelSource, PointsTilesAndGridAgree) {
    const ShapeModelSource source(LumpyBody(16),
                                  std::make_shared<KernelUpsampler>(ResampleMethod::Cubic));
    terraingen::testing::ExpectPointsTilesAndGridAgree(
        source, RasterGrid{24, 12, GeoTransform::NorthUp(30.0, 50.0, 0.75)});
}

TEST(ShapeModelSource, FitsDiamondSquareToEachFace) {
    const auto given = std::make_shared<DiamondSquareUpsampler>();
    const ShapeModelSource source(LumpyBody(32), given);
    for (const auto& fitted : source.UpsamplersInUse()) {
        const auto* diamond = dynamic_cast<const DiamondSquareUpsampler*>(fitted.get());
        ASSERT_NE(diamond, nullptr);
        EXPECT_NE(fitted, given);
        EXPECT_TRUE(diamond->Options().fractalDimension.has_value());
    }
    EXPECT_EQ(source.Body().Shape()->Q(), 32u);
}
