#include "ernie/camera/physical_camera.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using ernie::PhysicalCamera;
using ernie::Scene;
namespace tokens = ernie::tokens;

TEST(PhysicalCamera, ExposureScaleFollowsUsd) {
    PhysicalCamera camera;
    EXPECT_DOUBLE_EQ(camera.LinearExposureScale(), 1.0) << "USD's defaults";
    camera.exposureTime = 0.01;
    camera.iso = 200.0;
    camera.fStop = 8.0;
    camera.responsivity = 0.5;
    camera.exposureStops = 1.0;
    EXPECT_DOUBLE_EQ(camera.LinearExposureScale(), 0.5 * 0.01 * 2.0 * 2.0 / 64.0);
}

TEST(PhysicalCamera, IntrinsicsFollowTheApertureAndOffsets) {
    PhysicalCamera camera;
    camera.width = 640;
    camera.height = 480;
    camera.focalLengthMm = 24.0;
    camera.verticalApertureMm = 4.8;
    EXPECT_DOUBLE_EQ(camera.PixelPitchMetres(), 1e-5);
    EXPECT_DOUBLE_EQ(camera.FocalLengthPixels(), 2400.0);
    EXPECT_EQ(camera.PrincipalPoint(), glm::dvec2(320.0, 240.0));
    EXPECT_NEAR(camera.VerticalFov(), 2.0 * std::atan(2.4 / 24.0), 1e-15);
    camera.horizontalApertureOffsetMm = 0.1;
    camera.verticalApertureOffsetMm = 0.05;
    EXPECT_EQ(camera.PrincipalPoint(), glm::dvec2(330.0, 235.0)) << "offsets up the image";
}

TEST(PhysicalCamera, DistortionRoundTrips) {
    ernie::LensModel lens;
    EXPECT_FALSE(lens.HasDistortion());
    EXPECT_EQ(lens.Distort({0.3, -0.2}), glm::dvec2(0.3, -0.2));
    lens.k1 = -0.2;
    lens.k2 = 0.05;
    lens.k3 = -0.01;
    lens.p1 = 0.001;
    lens.p2 = -0.002;
    EXPECT_TRUE(lens.HasDistortion());
    const glm::dvec2 p(0.3, -0.2);
    const double r2 = 0.13;
    const double radial = 1.0 + r2 * (-0.2 + r2 * (0.05 + r2 * -0.01));
    const glm::dvec2 expected(0.3 * radial + 2.0 * 0.001 * 0.3 * -0.2 + -0.002 * (r2 + 2.0 * 0.09),
                              -0.2 * radial + 0.001 * (r2 + 2.0 * 0.04) +
                                  2.0 * -0.002 * 0.3 * -0.2);
    EXPECT_NEAR(lens.Distort(p).x, expected.x, 1e-15);
    EXPECT_NEAR(lens.Distort(p).y, expected.y, 1e-15);
    const glm::dvec2 back = lens.Undistort(lens.Distort(p));
    EXPECT_NEAR(back.x, p.x, 1e-9);
    EXPECT_NEAR(back.y, p.y, 1e-9);
}

TEST(PhysicalCamera, ElectronsFollowTheCameraEquation) {
    PhysicalCamera camera;
    camera.width = 100;
    camera.height = 100;
    camera.verticalApertureMm = 1.0;
    camera.fStop = 4.0;
    camera.exposureTime = 0.002;
    camera.bands = {500.0, 600.0, 1};
    camera.sensor.channels = {{"flat", ernie::Spectrum::Constant(0.5)}};
    camera.lens.transmission = 0.9;
    camera.lens.vignetting = true;
    const std::vector<float> radiance{2.0f};
    const double irradiance = 2.0 * M_PI * 0.9 / (4.0 * 16.0);
    const double energy = irradiance * 100.0 * 1e-10 * 0.002;
    const double photons = energy * 550e-9 / (6.62607015e-34 * 299792458.0);
    EXPECT_NEAR(camera.Electrons(radiance, 0) / (photons * 0.5), 1.0, 1e-6);

    const double off = camera.Electrons(radiance, 0, {1.0, 0.0});
    EXPECT_NEAR(off / camera.Electrons(radiance, 0), 0.25, 1e-12) << "cos^4 of 45 degrees";
    camera.lens.vignetting = false;
    EXPECT_NEAR(camera.Electrons(radiance, 0, {1.0, 0.0}), camera.Electrons(radiance, 0), 1e-9);
    EXPECT_THROW(camera.Electrons(std::vector<float>{1.0f, 2.0f}, 0), std::invalid_argument);
}

TEST(PhysicalCamera, SensorPresetsAndGain) {
    const ernie::SensorModel rgb = ernie::SensorModel::Rgb();
    ASSERT_EQ(rgb.channels.size(), 3u);
    EXPECT_GT(rgb.channels[0].quantumEfficiency(610.0), rgb.channels[2].quantumEfficiency(610.0));
    EXPECT_GT(rgb.channels[2].quantumEfficiency(460.0), rgb.channels[0].quantumEfficiency(460.0));
    const ernie::SensorModel mono = ernie::SensorModel::Mono();
    ASSERT_EQ(mono.channels.size(), 1u);
    EXPECT_GT(mono.channels[0].quantumEfficiency(600.0), 0.5);
    EXPECT_NEAR(rgb.ElectronsPerDn(100.0), 20000.0 / 4095.0, 1e-9);
    EXPECT_NEAR(rgb.ElectronsPerDn(400.0), 20000.0 / 4095.0 / 4.0, 1e-9) << "ISO raises gain";
}

TEST(PhysicalCamera, ReadsUsdAndErnieAttributes) {
    Scene scene;
    scene.Define("/Camera", tokens::kCamera)
        .Set(tokens::kFocalLength, 35.0f)
        .Set(tokens::kVerticalAperture, 10.0f)
        .Set(tokens::kVerticalApertureOffset, 0.5)
        .Set(tokens::kClippingRange, glm::vec2(0.5f, 500.0f))
        .Set(tokens::kCameraExposure, -1.0)
        .Set(tokens::kExposureTime, 0.004)
        .Set(tokens::kExposureIso, 400.0)
        .Set(tokens::kExposureFStop, 5.6)
        .Set(tokens::kSpectralRange, glm::dvec2(400.0, 1000.0))
        .Set(tokens::kSpectralBands, 24)
        .Set(tokens::kLensDistortion, std::vector<float>{-0.1f, 0.01f})
        .Set(tokens::kLensVignetting, false)
        .Set(tokens::kSensorType, "mono")
        .Set(std::string(tokens::kSensorQePrefix) + "mono",
             std::vector<float>{400, 0.3f, 900, 0.3f})
        .Set(tokens::kSensorAdcBits, 14)
        .Set(tokens::kSensorNoise, false)
        .Set(tokens::kSensorSeed, 7);
    const PhysicalCamera camera = ernie::PhysicalCameraOf(scene, "/Camera", 64, 48);
    EXPECT_EQ(camera.width, 64u);
    EXPECT_DOUBLE_EQ(camera.focalLengthMm, 35.0);
    EXPECT_DOUBLE_EQ(camera.verticalApertureOffsetMm, 0.5);
    EXPECT_DOUBLE_EQ(camera.nearPlane, 0.5);
    EXPECT_DOUBLE_EQ(camera.exposureStops, -1.0);
    EXPECT_DOUBLE_EQ(camera.iso, 400.0);
    EXPECT_DOUBLE_EQ(camera.fStop, 5.6);
    EXPECT_EQ(camera.bands, (ernie::SpectralBands{400.0, 1000.0, 24}));
    EXPECT_DOUBLE_EQ(camera.lens.k1, -0.1f);
    EXPECT_DOUBLE_EQ(camera.lens.k3, 0.0);
    EXPECT_FALSE(camera.lens.vignetting);
    ASSERT_EQ(camera.sensor.channels.size(), 1u);
    EXPECT_NEAR(camera.sensor.channels[0].quantumEfficiency(650.0), 0.3, 1e-6);
    EXPECT_EQ(camera.sensor.adcBits, 14);
    EXPECT_FALSE(camera.sensor.noise);
    EXPECT_EQ(camera.sensor.seed, 7u);

    scene.Set("/Camera", tokens::kSensorType, "infrared");
    EXPECT_THROW(ernie::PhysicalCameraOf(scene, "/Camera", 64, 48), std::invalid_argument);
    scene.Set("/Camera", tokens::kSensorType, "rgb");
    scene.Set("/Camera", tokens::kExposureTime, 0.0);
    EXPECT_THROW(ernie::PhysicalCameraOf(scene, "/Camera", 64, 48), std::invalid_argument);
    scene.Define("/NotACamera", tokens::kXform);
    EXPECT_THROW(ernie::PhysicalCameraOf(scene, "/NotACamera", 64, 48), std::invalid_argument);
}
