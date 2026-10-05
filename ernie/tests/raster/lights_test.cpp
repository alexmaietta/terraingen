#include "ernie/raster/raster_delegate.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

using ernie::Light;
using ernie::LightType;
using ernie::Path;
using ernie::Scene;
using ernie::ShadowCascade;
namespace tokens = ernie::tokens;

namespace {

double LuminanceIntegral() {
    double sum = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        sum += ernie::CieXyz(w).y;
    }
    return sum;
}

/// Whether `p` lies inside the clip volume of `view`.
bool Sees(const ShadowCascade& view, const glm::vec3& p) {
    const glm::vec4 clip = view.lightViewProjection * glm::vec4(p, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f && ndc.z >= 0.0f && ndc.z <= 1.0f;
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
    scene.Define("/Rect", tokens::kRectLight);
    EXPECT_THROW(ernie::LightOf(scene, "/Rect", bands), std::invalid_argument);
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
    ernie::RasterPrims prims = ernie::ResolveRasterPrims(scene, {});
    EXPECT_EQ(prims.light, Path());
    EXPECT_EQ(prims.lights, std::vector<Path>{"/World/Lamp"});
    ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 8, 8);
    EXPECT_FALSE(frame.sun) << "lamps alone light the scene";
    ASSERT_EQ(frame.lights.size(), 1u);

    scene.Define("/World/Sun", tokens::kDistantLight).Set(tokens::kAngle, 2.0f);
    scene.Define("/World/Earth", tokens::kDistantLight);
    prims = ernie::ResolveRasterPrims(scene, {});
    EXPECT_EQ(prims.light, Path("/World/Earth")) << "the first distant light is the sun";
    EXPECT_EQ(prims.lights, (std::vector<Path>{"/World/Lamp", "/World/Sun"}));
    frame = ernie::FrameParamsOf(scene, prims, 8, 8);
    EXPECT_TRUE(frame.sun);
    EXPECT_EQ(frame.sunAngle, 0.53f) << "UsdLux's default angle";
    EXPECT_EQ(frame.lights.size(), 2u);
    scene.Set("/World/Earth", tokens::kAngle, 2.0f);
    EXPECT_EQ(ernie::FrameParamsOf(scene, prims, 8, 8).sunAngle, 2.0f);
}

TEST(FitLightShadowViews, CoversTheSceneFromEachKindOfLight) {
    const glm::vec3 low(-10.0f, -10.0f, 0.0f);
    const glm::vec3 high(10.0f, 10.0f, 2.0f);
    Light distant;
    distant.type = LightType::Distant;
    distant.direction = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const std::vector<ShadowCascade> ortho = ernie::FitLightShadowViews(distant, low, high, 512);
    ASSERT_EQ(ortho.size(), 1u);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                               (i & 4) ? high.z : low.z);
        EXPECT_TRUE(Sees(ortho[0], corner)) << "corner " << i;
    }
    EXPECT_NEAR(ortho[0].texelWorldSize, glm::length(high - low) / 512.0f, 1e-4f);

    Light spot;
    spot.position = glm::vec3(0.0f, 0.0f, 10.0f);
    spot.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    spot.coneAngle = 30.0f;
    const std::vector<ShadowCascade> one = ernie::FitLightShadowViews(spot, low, high, 512);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_TRUE(Sees(one[0], {0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(Sees(one[0], {5.0f, 0.0f, 0.0f})) << "inside the 30 degree cone";
    EXPECT_FALSE(Sees(one[0], {9.0f, 0.0f, 0.0f})) << "outside it";
    EXPECT_NEAR(one[0].texelWorldSize, 2.0f * std::tan(glm::radians(30.0f)) / 512.0f, 1e-6f);

    spot.coneAngle = 180.0f;
    const std::vector<ShadowCascade> cube = ernie::FitLightShadowViews(spot, low, high, 512);
    ASSERT_EQ(cube.size(), 6u);
    const glm::vec3 axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (std::size_t face = 0; face < 6; ++face) {
        EXPECT_TRUE(Sees(cube[face], spot.position + 5.0f * axes[face])) << "face " << face;
        EXPECT_FALSE(Sees(cube[face], spot.position - 5.0f * axes[face])) << "face " << face;
    }

    spot.shadows = false;
    EXPECT_TRUE(ernie::FitLightShadowViews(spot, low, high, 512).empty());
}
