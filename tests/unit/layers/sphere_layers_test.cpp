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
using terraingen::Surface;
using terraingen::Vec2d;

namespace {

constexpr double kMoon = 1737400.0;
const Surface kSphere = Surface::Sphere(kMoon);

/// A longitude-latitude raster of `side` cells of `meters` (along a meridian) whose north-west
/// corner is at (`lon`, `lat`), filled with `fill`.
Raster<float> Geographic(double lon, double lat, double meters, std::size_t side,
                         float fill = 0.0f) {
    Raster<float> raster(side, side, fill);
    raster.SetTransform(GeoTransform::NorthUp(lon, lat, meters / kSphere.UnitLength()));
    return raster;
}

/// Applies `layer` to `raster` on one thread and on all of them, and checks both against point
/// queries at a few cells; returns the result.
Raster<float> CheckGridAndPoints(const Layer& layer, Raster<float> raster, double spacing,
                                 const terraingen::Ground* ground = nullptr) {
    Raster<float> threaded = raster;
    const Raster<float> before = raster;
    layer.Apply(raster, {spacing, 1, ground});
    layer.Apply(threaded, {spacing, 0, ground});
    for (std::size_t i = 0; i < raster.Size(); ++i) {
        EXPECT_EQ(raster.Values()[i], threaded.Values()[i]);
    }
    const std::size_t last = raster.Width() - 1;
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{0, 0}, std::pair<std::size_t, std::size_t>{last, 3},
          std::pair<std::size_t, std::size_t>{last / 2, last / 2},
          std::pair<std::size_t, std::size_t>{7, last}}) {
        const Vec2d center = raster.CellCenter(col, row);
        EXPECT_EQ(layer.ApplyAt(before(col, row), center.x, center.y, {spacing, 1, ground}),
                  raster(col, row))
            << col << " " << row;
    }
    return raster;
}

class Flat final : public terraingen::Ground {
  public:
    float HeightAt(double, double, double) const override {
        return 0.0f;
    }
};

}  // namespace

TEST(SphereLayers, NoiseIsSeamlessAcrossTheAntimeridianAndPoles) {
    terraingen::SyntheticTerrainParams params{{terraingen::NoiseLayer{}}};
    params.layers[0].fractal.wavelength = 300.0f;
    params.layers[0].fractal.amplitude = 20.0f;
    const terraingen::NoiseRelief noise(params, {}, kSphere);
    const Raster<float> grid = CheckGridAndPoints(noise, Geographic(179.99, 10.0, 20.0, 32), 20.0);
    EXPECT_NE(grid(0, 0), grid(31, 31));
    EXPECT_FLOAT_EQ(noise.ApplyAt(0.0f, 180.0, 10.0, {1.0}),
                    noise.ApplyAt(0.0f, -180.0, 10.0, {1.0}));
    EXPECT_NEAR(noise.ApplyAt(0.0f, 0.0, 90.0, {1.0}), noise.ApplyAt(0.0f, 123.0, 90.0, {1.0}),
                1e-4);
    for (std::uint32_t b = 0; b < terraingen::kNoiseBasisCount; ++b) {
        SCOPED_TRACE(b);
        terraingen::SyntheticTerrainParams basis = params;
        basis.layers[0].fractal.basis = static_cast<terraingen::NoiseBasis>(b);
        const terraingen::NoiseRelief relief(basis, {}, kSphere);
        EXPECT_FLOAT_EQ(relief.ApplyAt(0.0f, 180.0, 10.0, {1.0}),
                        relief.ApplyAt(0.0f, -180.0, 10.0, {1.0}));
    }
}

TEST(SphereLayers, ExposureReadsTheSurroundingsInMeters) {
    class EastSlope final : public terraingen::Ground {
      public:
        float HeightAt(double x, double y, double) const override {
            return static_cast<float>(0.5 * kSphere.Offset({30.0, 60.0}, {x, y}).x);
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
}

TEST(SphereLayers, RegionsMeasureMeters) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(1.0, 5.0);
    const terraingen::MaskedLayer circle(inner,
                                         terraingen::RegionShape::Circle({-40.0, 45.0}, 1000.0),
                                         200.0, terraingen::BlendShape::Linear, kSphere);
    const auto at = [&](double meters) {
        const Vec2d p = kSphere.Locate({-40.0, 45.0}, {meters, 0.0});
        return circle.ApplyAt(0.0f, p.x, p.y, {1.0});
    };
    EXPECT_FLOAT_EQ(at(900.0), 5.0f);
    EXPECT_NEAR(at(1100.0), 2.5f, 1e-3f);
    EXPECT_FLOAT_EQ(at(1300.0), 0.0f);
    CheckGridAndPoints(circle, Geographic(-40.05, 45.05, 25.0, 40), 25.0);
    const terraingen::MaskedLayer box(inner,
                                      terraingen::RegionShape::Rectangle({179.0, 0.0, 181.0, 1.0}),
                                      100.0, terraingen::BlendShape::Linear, kSphere);
    EXPECT_FLOAT_EQ(box.ApplyAt(0.0f, -179.5, 0.5, {1.0}), 5.0f);
    EXPECT_FLOAT_EQ(box.ApplyAt(0.0f, -178.0, 0.5, {1.0}), 0.0f);
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
    const terraingen::BedformLayer sphere(options, kSphere);
    const terraingen::BedformLayer plane(options);
    double onSphere = 0.0;
    double onPlane = 0.0;
    RandomStream random(3, 4);
    const int samples = 6000;
    for (int k = 0; k < samples; ++k) {
        const double e = 5000.0 * random.NextDouble();
        const double n = 5000.0 * random.NextDouble();
        const Vec2d p = kSphere.Locate({100.0, 44.0}, {e, n});
        onSphere += sphere.CoverAt(p.x, p.y);
        onPlane += plane.CoverAt(e, n);
    }
    EXPECT_NEAR(onSphere / samples, onPlane / samples, 0.05);
    EXPECT_GT(onSphere / samples, 0.2);
    CheckGridAndPoints(sphere, Geographic(100.0, 44.01, 1.0, 48), 1.0);
}

TEST(SphereLayers, BouldersHaveThePlaneDensityAndPlaceModelsLocally) {
    terraingen::BoulderLayerOptions options;
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 0.5;
    options.sizes.maxDiameter = 4.0;
    options.rendering.models.minDiameter = 2.0;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    const terraingen::BoulderLayer sphere(options, nullptr, kSphere);
    const terraingen::BoulderLayer plane(options);
    const double side = 120.0;
    const double dLat = side / kSphere.UnitLength();
    const double dLon = dLat / std::cos(50.0 * terraingen::kPi / 180.0);
    const auto onSphere = sphere.Boulders({20.0, 50.0, 20.0 + dLon, 50.0 + dLat}, 0.05);
    const auto onPlane = plane.Boulders({0.0, 0.0, side, side}, 0.05);
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
        sphere.Boulders(terraingen::RasterGrid::FromRaster(heights).Bounds(), 0.1, &flat);
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
    const terraingen::RoughnessLayer layer(options, kSphere);
    RandomStream random(5, 6);
    double sum = 0.0;
    const int pairs = 6000;
    for (int k = 0; k < pairs; ++k) {
        const Vec2d a{360.0 * random.NextDouble() - 180.0, 140.0 * random.NextDouble() - 70.0};
        const double azimuth = 2.0 * terraingen::kPi * random.NextDouble();
        const Vec2d b = kSphere.Locate(a, {0.25 * std::sin(azimuth), 0.25 * std::cos(azimuth)});
        const double d = static_cast<double>(layer.ApplyAt(0.0f, b.x, b.y, {0.001}) -
                                             layer.ApplyAt(0.0f, a.x, a.y, {0.001}));
        sum += d * d;
    }
    EXPECT_NEAR(std::sqrt(sum / pairs) / 0.25, 0.2, 0.02);
    CheckGridAndPoints(layer, Geographic(-60.0, -30.0, 0.05, 32), 0.05);
}
