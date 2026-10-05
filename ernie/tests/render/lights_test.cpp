#include "ernie/render/resolve.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

using ernie::Light;
using ernie::LightType;
using ernie::Path;
using ernie::Scene;
namespace tokens = ernie::tokens;

namespace {

double LuminanceIntegral() {
    double sum = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        sum += ernie::CieXyz(w).y;
    }
    return sum;
}

}  // namespace

TEST(LightOf, SphereLightsInRelativeUnitsCarryTheirSurfaceLuminance) {
    Scene scene;
    scene.Define("/Lamp", tokens::kSphereLight)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({1.0, 2.0, 10.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 0.0}))
        .Set(tokens::kIntensity, 4.0)
        .Set(tokens::kRadius, 0.5);
    const ernie::SpectralBands bands;
    const Light lamp = ernie::LightOf(scene, "/Lamp", bands);
    EXPECT_EQ(lamp.type, LightType::Sphere);
    EXPECT_EQ(lamp.position, glm::vec3(1.0f, 2.0f, 10.0f));
    EXPECT_NEAR(lamp.direction.z, -1.0f, 1e-6f) << "emits along its -z axis";
    EXPECT_EQ(lamp.radius, 0.5f);
    EXPECT_EQ(lamp.coneAngle, 180.0f) << "unshaped";
    ASSERT_EQ(lamp.spectrum.size(), 16u);
    const double luminous = 4.0 * M_PI * 0.25;
    EXPECT_NEAR(lamp.spectrum[8], luminous / (ernie::kLuminousEfficacy * LuminanceIntegral()), 1e-7)
        << "luminous intensity = luminance x pi r^2";

    scene.Set("/Lamp", tokens::kTreatAsPoint, true);
    scene.Set("/Lamp", tokens::kShapingConeAngle, 30.0f);
    scene.Set("/Lamp", tokens::kShapingConeSoftness, 0.2f);
    scene.Set("/Lamp", tokens::kShadowEnable, false);
    const Light spot = ernie::LightOf(scene, "/Lamp", bands);
    EXPECT_EQ(spot.radius, 0.0f);
    EXPECT_EQ(spot.coneAngle, 30.0f);
    EXPECT_FLOAT_EQ(spot.coneSoftness, 0.2f);
    EXPECT_FALSE(spot.shadows);
    EXPECT_NEAR(spot.spectrum[8], 4.0 / (ernie::kLuminousEfficacy * LuminanceIntegral()), 1e-7)
        << "a point's luminous intensity is its intensity";
}

TEST(LightOf, PhysicalLampsSpreadTheirPowerOverTheSphere) {
    Scene scene;
    scene.Define("/Lamp", tokens::kSphereLight).Set(tokens::kLightPower, 470.0);
    const ernie::SpectralBands bands;
    for (const float value : ernie::LightOf(scene, "/Lamp", bands).spectrum) {
        EXPECT_NEAR(value, 1.0 / (4.0 * M_PI), 1e-6) << "470 W flat over 470 nm, over 4 pi sr";
    }
    scene.Set("/Lamp", tokens::kLightSpectrum, std::vector<float>{400.0f, 2.0f, 700.0f, 2.0f});
    for (const float value : ernie::LightOf(scene, "/Lamp", {400.0, 700.0, 3}).spectrum) {
        EXPECT_NEAR(value, 470.0 / 300.0 / (4.0 * M_PI), 1e-5) << "the spectrum's shape";
    }
    scene.Clear("/Lamp", tokens::kLightPower);
    EXPECT_EQ(ernie::LightOf(scene, "/Lamp", {400.0, 700.0, 3}).spectrum,
              (std::vector<float>{2.0f, 2.0f, 2.0f}))
        << "a spectrum alone is the intensity";

    scene.Set("/Lamp", tokens::kLightPower, -1.0);
    EXPECT_THROW(ernie::LightOf(scene, "/Lamp", bands), std::invalid_argument);
    scene.Define("/Sky", tokens::kDomeLight);
    EXPECT_THROW(ernie::LightOf(scene, "/Sky", bands), std::invalid_argument) << "see DomeOf";
}

TEST(LightOf, DistantLightsShineFromTheirAxis) {
    Scene scene;
    scene.Define("/Earth", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({0.0, 1.0, 1.0}))
        .Set(tokens::kLightIrradiance, 0.1);
    const ernie::SpectralBands bands;
    const Light earth = ernie::LightOf(scene, "/Earth", bands);
    EXPECT_EQ(earth.type, LightType::Distant);
    EXPECT_NEAR(earth.direction.y, std::sqrt(0.5f), 1e-6f);
    EXPECT_EQ(earth.spectrum, ernie::SunIrradianceOf(scene, "/Earth", bands));
}

TEST(FrameParamsOf, ResolvesEveryLightAndTheSunsDisk) {
    Scene scene;
    scene.Define("/World/Camera", tokens::kCamera);
    scene.Define("/World/Lamp", tokens::kSphereLight);
    scene.Define("/World/Off", tokens::kSphereLight).Set(tokens::kVisibility, tokens::kInvisible);
    ernie::RenderPrims prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.light, Path());
    EXPECT_EQ(prims.lights, std::vector<Path>{"/World/Lamp"});
    ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 8, 8);
    EXPECT_FALSE(frame.sun) << "lamps alone light the scene";
    ASSERT_EQ(frame.lights.size(), 1u);

    scene.Define("/World/Sun", tokens::kDistantLight).Set(tokens::kAngle, 2.0f);
    scene.Define("/World/Earth", tokens::kDistantLight);
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.light, Path("/World/Earth")) << "the first distant light is the sun";
    EXPECT_EQ(prims.lights, (std::vector<Path>{"/World/Lamp", "/World/Sun"}));
    frame = ernie::FrameParamsOf(scene, prims, 8, 8);
    EXPECT_TRUE(frame.sun);
    EXPECT_EQ(frame.sunAngle, 0.53f) << "UsdLux's default angle";
    EXPECT_EQ(frame.lights.size(), 2u);
    scene.Set("/World/Earth", tokens::kAngle, 2.0f);
    EXPECT_EQ(ernie::FrameParamsOf(scene, prims, 8, 8).sunAngle, 2.0f);
}

TEST(LightOf, RectAndDiskLightsTakeTheirSizeAndRadiance) {
    Scene scene;
    scene.Define("/Panel", tokens::kRectLight)
        .Set(tokens::kTransform,
             glm::scale(ernie::LookAtTransform({0.0, 0.0, 5.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}),
                        glm::dvec3(2.0, 1.0, 1.0)))
        .Set(tokens::kWidth, 3.0)
        .Set(tokens::kHeight, 4.0)
        .Set(tokens::kIntensity, 10.0);
    const ernie::SpectralBands bands;
    const Light panel = ernie::LightOf(scene, "/Panel", bands);
    EXPECT_EQ(panel.type, LightType::Rect);
    EXPECT_EQ(panel.size, glm::vec2(6.0f, 4.0f)) << "scaled by the transform";
    EXPECT_NEAR(panel.direction.z, -1.0f, 1e-6f);
    EXPECT_NEAR(glm::dot(panel.tangent, panel.direction), 0.0f, 1e-6f);
    EXPECT_NEAR(panel.spectrum[8], 10.0 / (ernie::kLuminousEfficacy * LuminanceIntegral()), 1e-7)
        << "relative units are the face's luminance";

    scene.Set("/Panel", tokens::kLightPower, 470.0 * M_PI * 24.0);
    EXPECT_NEAR(ernie::LightOf(scene, "/Panel", bands).spectrum[3], 1.0, 1e-5)
        << "power = pi x area x radiance for a Lambertian face";

    scene.Define("/Disk", tokens::kDiskLight).Set(tokens::kRadius, 2.0);
    const Light disk = ernie::LightOf(scene, "/Disk", bands);
    EXPECT_EQ(disk.type, LightType::Disk);
    EXPECT_EQ(disk.radius, 2.0f);
    scene.Set("/Disk", tokens::kRadius, 0.0);
    EXPECT_THROW(ernie::LightOf(scene, "/Disk", bands), std::invalid_argument);
}

namespace {

/// The irradiance factor of `sh` at unit normal `n`.
glm::vec3 Evaluate(const std::array<glm::vec3, 9>& sh, const glm::vec3& n) {
    const float basis[9] = {0.282095f,
                            0.488603f * n.y,
                            0.488603f * n.z,
                            0.488603f * n.x,
                            1.092548f * n.x * n.y,
                            1.092548f * n.y * n.z,
                            0.315392f * (3.0f * n.z * n.z - 1.0f),
                            1.092548f * n.x * n.z,
                            0.546274f * (n.x * n.x - n.y * n.y)};
    glm::vec3 sum(0.0f);
    for (int k = 0; k < 9; ++k) {
        sum += sh[k] * basis[k];
    }
    return sum;
}

ernie::Tensor UpperHemisphere(std::int64_t rows, std::int64_t cols) {
    std::vector<float> map(static_cast<std::size_t>(rows * cols * 4), 0.0f);
    std::fill(map.begin(), map.begin() + static_cast<std::ptrdiff_t>(map.size() / 2), 1.0f);
    return ernie::Tensor::Copy<float>(map, {rows, cols, 4});
}

}  // namespace

TEST(DomeIrradianceSh, ProjectsUniformAndHemisphericalMaps) {
    std::vector<float> ones(32 * 64 * 3, 1.0f);
    const auto uniform =
        ernie::DomeIrradianceSh(ernie::Tensor::Copy<float>(ones, {32, 64, 3}), glm::dmat3(1.0));
    EXPECT_NEAR(uniform[0].g, std::sqrt(4.0 * M_PI), 0.01);
    for (const glm::vec3 n : {glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)}) {
        EXPECT_NEAR(Evaluate(uniform, n).r, 1.0f, 0.01f) << "E = pi L from every side";
    }

    const auto upper = ernie::DomeIrradianceSh(UpperHemisphere(64, 128), glm::dmat3(1.0));
    EXPECT_NEAR(Evaluate(upper, {0, 0, 1}).g, 1.0f, 0.05f) << "a sky facing up";
    EXPECT_NEAR(Evaluate(upper, {0, 0, -1}).g, 0.0f, 0.05f) << "and nothing facing down";
    EXPECT_NEAR(Evaluate(upper, {1, 0, 0}).g, 0.5f, 0.01f) << "half from a wall";

    const glm::dmat3 flip(glm::rotate(glm::dmat4(1.0), M_PI, glm::dvec3(1.0, 0.0, 0.0)));
    const auto flipped = ernie::DomeIrradianceSh(UpperHemisphere(64, 128), flip);
    EXPECT_NEAR(Evaluate(flipped, {0, 0, -1}).g, 1.0f, 0.05f);

    EXPECT_THROW(
        ernie::DomeIrradianceSh(ernie::Tensor::Copy<float>(ones, {32 * 64, 3}), glm::dmat3(1.0)),
        std::invalid_argument);
}

TEST(DomeOf, ResolvesTheDomeAndTurnsOffTheDefaultSun) {
    Scene scene;
    scene.Define("/World/Camera", tokens::kCamera);
    scene.Define("/World/Sky", tokens::kDomeLight).Set(tokens::kIntensity, 2.0);
    const ernie::RenderPrims prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.dome, Path("/World/Sky"));
    const ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 8, 8);
    EXPECT_FALSE(frame.sun);
    ASSERT_TRUE(frame.dome.has_value());
    EXPECT_NEAR(frame.dome->radiance[8], 2.0 / (ernie::kLuminousEfficacy * LuminanceIntegral()),
                1e-7);
    EXPECT_NEAR(frame.dome->sh[0].r, std::sqrt(4.0 * M_PI), 1e-5);
    EXPECT_THROW(ernie::DomeOf(scene, "/World/Camera", {}), std::invalid_argument);
}
