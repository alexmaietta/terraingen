// SPDX-License-Identifier: BSD-3-Clause

#include "support/boulder_records.h"
#include "support/golombek_rapp.h"
#include "terraingen/terraingen.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::Layer;
using terraingen::LayerContext;
using terraingen::RandomStream;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::Surface;
using terraingen::Vec2d;

namespace {

constexpr double kMoon = 1737400.0;
const Surface kSphere = Surface::Sphere(kMoon);

/// A longitude-latitude raster of `side` cells of `meters` (along a meridian) whose north-west
/// corner is at (`lon`, `lat`), filled with `fill`.
Raster<float> Geographic(double lon, double lat, double meters, std::size_t side,
                         float fill = 0.0f) {
    Raster<float> raster(
        RasterGrid{side, side, GeoTransform::NorthUp(lon, lat, meters / kSphere.UnitLength())},
        fill);
    return raster;
}

/// The context of `threads` threads at `spacing` meters on the sphere over `ground`.
LayerContext On(double spacing, unsigned threads = 1, const terraingen::Ground* ground = nullptr) {
    LayerContext context{spacing, threads, ground};
    context.surface = &kSphere;
    return context;
}

/// Applies `layer` to `raster` on the sphere on one thread and on all of them, and checks both
/// against point queries at a few cells; returns the result.
Raster<float> CheckGridAndPoints(const Layer& layer, Raster<float> raster, double spacing,
                                 const terraingen::Ground* ground = nullptr) {
    Raster<float> threaded = raster;
    const Raster<float> before = raster;
    layer.Apply(raster, On(spacing, 1, ground));
    layer.Apply(threaded, On(spacing, 0, ground));
    for (std::size_t i = 0; i < raster.Size(); ++i) {
        EXPECT_EQ(raster.Values()[i], threaded.Values()[i]);
    }
    const std::size_t last = raster.Width() - 1;
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{0, 0}, std::pair<std::size_t, std::size_t>{last, 3},
          std::pair<std::size_t, std::size_t>{last / 2, last / 2},
          std::pair<std::size_t, std::size_t>{7, last}}) {
        const Vec2d center = raster.CellCenter(col, row);
        EXPECT_EQ(layer.ApplyAt(before(col, row), center.x, center.y, On(spacing, 1, ground)),
                  raster(col, row))
            << col << " " << row;
    }
    return raster;
}

class Flat final : public terraingen::Ground {
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = 0.0f;
        }
    }
};

}  // namespace

TEST(SphereLayers, NoiseIsSeamlessAcrossTheAntimeridianAndPoles) {
    std::vector<terraingen::Fractal> params{terraingen::Fractal{}};
    params[0].wavelength = 300.0f;
    params[0].amplitude = 20.0f;
    const terraingen::NoiseLayer noise(params);
    const Raster<float> grid = CheckGridAndPoints(noise, Geographic(179.99, 10.0, 20.0, 32), 20.0);
    EXPECT_NE(grid(0, 0), grid(31, 31));
    EXPECT_FLOAT_EQ(noise.ApplyAt(0.0f, 180.0, 10.0, On(1.0)),
                    noise.ApplyAt(0.0f, -180.0, 10.0, On(1.0)));
    EXPECT_NEAR(noise.ApplyAt(0.0f, 0.0, 90.0, On(1.0)), noise.ApplyAt(0.0f, 123.0, 90.0, On(1.0)),
                1e-4);
    for (std::uint32_t b = 0; b < terraingen::kNoiseBasisCount; ++b) {
        SCOPED_TRACE(b);
        std::vector<terraingen::Fractal> basis = params;
        basis[0].basis = static_cast<terraingen::NoiseBasis>(b);
        const terraingen::NoiseLayer relief(basis);
        EXPECT_FLOAT_EQ(relief.ApplyAt(0.0f, 180.0, 10.0, On(1.0)),
                        relief.ApplyAt(0.0f, -180.0, 10.0, On(1.0)));
    }
}

TEST(SphereLayers, NoiseOnASphereIsThe3DNoiseItWasBuiltWithBefore) {
    std::vector<terraingen::Fractal> params{terraingen::Fractal{}};
    params[0].wavelength = 300.0f;
    params[0].amplitude = 20.0f;
    const terraingen::NoiseLayer noise(params);
    EXPECT_EQ(noise.ApplyAt(1.0f, 12.0, 34.0, On(1.0)),
              1.0f + terraingen::FractalStackAt(params, {}, kSphere, 12.0, 34.0));
    EXPECT_NE(noise.ApplyAt(1.0f, 12.0, 34.0, On(1.0)), noise.ApplyAt(1.0f, 12.0, 34.0, {1.0}))
        << "a plane without the context's surface";
}

TEST(SphereLayers, ExposureReadsTheSurroundingsInMeters) {
    class EastSlope final : public terraingen::Ground {
      protected:
        void FillSamples(terraingen::SampleBlock& samples, double,
                         unsigned /*threads*/) const override {
            for (std::size_t i = 0; i < samples.Size(); ++i) {
                samples.Height(i) =
                    static_cast<float>(0.5 * kSphere.Offset({30.0, 60.0}, samples.Position(i)).x);
            }
        }
    } ground;
    terraingen::ExposureParams params;
    params.slopeSoil = 0.2;
    params.slopeBoulder = 0.4;
    const terraingen::ExposureField field(params, kSphere);
    EXPECT_NEAR(field.Exposure(ground, 30.0, 60.0, 1.0), 1.0, 1e-6);
    params.slopeSoil = 0.6;
    params.slopeBoulder = 0.8;
    EXPECT_NEAR(terraingen::ExposureField(params, kSphere).Exposure(ground, 30.0, 60.0, 1.0), 0.0,
                1e-6);
    terraingen::ExposureParams patches;
    patches.floorCover = 0.5;
    const terraingen::ExposureField patchy(patches, kSphere);
    const Flat flat;
    double sum = 0.0;
    RandomStream random(1, 2);
    for (int k = 0; k < 2000; ++k) {
        sum += patchy.Exposure(flat, 360.0 * random.NextDouble() - 180.0,
                               160.0 * random.NextDouble() - 80.0, 1.0);
    }
    EXPECT_NEAR(sum / 2000.0, 0.5, 0.1);
    const terraingen::LayerExposure layered(params, 1.0, "Rocks");
    EXPECT_EQ(layered.At(30.0, 60.0, &ground, kSphere),
              terraingen::ExposureField(params, kSphere).Exposure(ground, 30.0, 60.0, 1.0));
}

TEST(SphereLayers, RegionsMeasureMeters) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(1.0, 5.0);
    const terraingen::MaskedLayer circle(inner,
                                         terraingen::RegionShape::Circle({-40.0, 45.0}, 1000.0),
                                         200.0, terraingen::BlendShape::Linear);
    const auto at = [&](double meters) {
        const Vec2d p = kSphere.Locate({-40.0, 45.0}, {meters, 0.0});
        return circle.ApplyAt(0.0f, p.x, p.y, On(1.0));
    };
    EXPECT_FLOAT_EQ(at(900.0), 5.0f);
    EXPECT_NEAR(at(1100.0), 2.5f, 1e-3f);
    EXPECT_FLOAT_EQ(at(1300.0), 0.0f);
    CheckGridAndPoints(circle, Geographic(-40.05, 45.05, 25.0, 40), 25.0);
    const terraingen::MaskedLayer box(inner,
                                      terraingen::RegionShape::Rectangle({179.0, 0.0, 181.0, 1.0}),
                                      100.0, terraingen::BlendShape::Linear);
    EXPECT_FLOAT_EQ(box.ApplyAt(0.0f, -179.5, 0.5, On(1.0)), 5.0f);
    EXPECT_FLOAT_EQ(box.ApplyAt(0.0f, -178.0, 0.5, On(1.0)), 0.0f);
    const Raster<float> wrapped =
        CheckGridAndPoints(box, Geographic(-179.0 - 0.003, 0.5, 30.0, 16), 30.0);
    EXPECT_GT(wrapped(0, 8), 0.0f);
}

TEST(SphereLayers, BedformsCoverAsMuchGroundAsOnAPlane) {
    terraingen::BedformOptions options;
    options.cover = 0.4;
    options.fieldSize = 100.0;
    options.wavelength = 5.0;
    options.height = 0.5;
    const terraingen::BedformLayer layer(options);
    double onSphere = 0.0;
    double onPlane = 0.0;
    RandomStream random(3, 4);
    const int samples = 6000;
    for (int k = 0; k < samples; ++k) {
        const double e = 5000.0 * random.NextDouble();
        const double n = 5000.0 * random.NextDouble();
        const Vec2d p = kSphere.Locate({100.0, 44.0}, {e, n});
        onSphere += layer.MaterialCoverAt(p.x, p.y, On(1.0));
        onPlane += layer.MaterialCoverAt(e, n, {1.0});
    }
    EXPECT_NEAR(onSphere / samples, onPlane / samples, 0.05);
    EXPECT_GT(onSphere / samples, 0.2);
    CheckGridAndPoints(layer, Geographic(100.0, 44.01, 1.0, 48), 1.0);
}

TEST(SphereLayers, BouldersHaveThePlaneDensityAndPlaceModelsLocally) {
    terraingen::BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 0.5;
    options.sizes.maxDiameter = 4.0;
    options.rendering.modelThreshold.minDiameter = 2.0;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    const terraingen::BoulderLayer sphere(options, nullptr, kSphere);
    const terraingen::BoulderLayer plane(options);
    const double side = 120.0;
    const double dLat = side / kSphere.UnitLength();
    const double dLon = dLat / std::cos(50.0 * terraingen::kPi / 180.0);
    const auto onSphere =
        terraingen::testing::Records(sphere.Boulders({20.0, 50.0, 20.0 + dLon, 50.0 + dLat}, 0.05));
    const auto onPlane = terraingen::testing::Records(plane.Boulders({0.0, 0.0, side, side}, 0.05));
    const double expected = static_cast<double>(onPlane.size());
    ASSERT_GT(expected, 100.0);
    EXPECT_NEAR(static_cast<double>(onSphere.size()), expected, 5.0 * std::sqrt(expected));
    const Flat flat;
    Raster<float> heights = Geographic(20.0, 50.0 + 20.0 / kSphere.UnitLength(), 0.1, 200);
    const Raster<float> placed = CheckGridAndPoints(sphere, heights, 0.1, &flat);
    float highest = 0.0f;
    for (const float value : placed.Values()) {
        highest = std::max(highest, value);
    }
    EXPECT_GT(highest, 0.05f);
    const auto boulders =
        terraingen::testing::Records(sphere.Boulders(heights.Grid().Bounds(), 0.1, &flat));
    for (const auto& boulder : sphere.Models(boulders)) {
        EXPECT_LT(std::fabs(boulder.placement.pose.translation.x), 1e-9);
        EXPECT_LT(std::fabs(boulder.placement.pose.translation.y), 1e-9);
    }
}

TEST(SphereLayers, RoughnessHasTheRmsSlopeAskedFor) {
    terraingen::RoughnessOptions options;
    options.rmsSlope = 0.2;
    options.baseline = 0.25;
    options.maxWavelength = 2.0;
    options.minWavelength = 0.02;
    const terraingen::RoughnessLayer layer(options);
    RandomStream random(5, 6);
    double sum = 0.0;
    const int pairs = 6000;
    for (int k = 0; k < pairs; ++k) {
        const Vec2d a{360.0 * random.NextDouble() - 180.0, 140.0 * random.NextDouble() - 70.0};
        const double azimuth = 2.0 * terraingen::kPi * random.NextDouble();
        const Vec2d b = kSphere.Locate(a, {0.25 * std::sin(azimuth), 0.25 * std::cos(azimuth)});
        const double d = static_cast<double>(layer.ApplyAt(0.0f, b.x, b.y, On(0.001)) -
                                             layer.ApplyAt(0.0f, a.x, a.y, On(0.001)));
        sum += d * d;
    }
    EXPECT_NEAR(std::sqrt(sum / pairs) / 0.25, 0.2, 0.02);
    EXPECT_NEAR(layer.RmsSlope(0.25, kSphere), 0.2, 1e-12);
    CheckGridAndPoints(layer, Geographic(-60.0, -30.0, 0.05, 32), 0.05);
}
