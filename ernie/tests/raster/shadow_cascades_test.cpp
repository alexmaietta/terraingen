#include "ernie/camera/orbit_camera.h"
#include "ernie/raster/shadow_cascades.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>

using ernie::CameraView;
using ernie::FitCascades;
using ernie::OrbitCamera;
using ernie::PracticalSplits;
using ernie::ShadowCascade;

namespace {

constexpr std::uint32_t kResolution = 1024;
const glm::vec3 kSceneMin(-500.0f, -500.0f, -30.0f);
const glm::vec3 kSceneMax(500.0f, 500.0f, 40.0f);

CameraView TestCamera() {
    OrbitCamera camera;
    camera.target = {50.0f, -20.0f, 0.0f};
    camera.distance = 400.0f;
    camera.yaw = 0.4f;
    camera.pitch = 0.5f;
    return camera.MakeView(1.6f);
}

glm::vec3 Project(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 clip = m * glm::vec4(p, 1.0f);
    return glm::vec3(clip) / clip.w;
}

glm::vec3 SliceCorner(const CameraView& camera, float depth, int i) {
    const float tanY = std::tan(0.5f * camera.verticalFov);
    const float tanX = tanY * camera.aspect;
    const glm::vec4 inView((i & 1 ? 1.0f : -1.0f) * tanX * depth,
                           (i & 2 ? 1.0f : -1.0f) * tanY * depth, -depth, 1.0f);
    return glm::vec3(glm::inverse(camera.view) * inView);
}

}  // namespace

TEST(PracticalSplits, SpansNearToFarInIncreasingOrder) {
    const auto splits = PracticalSplits(1.0f, 1000.0f, 4, 0.7f);
    ASSERT_EQ(splits.size(), 5u);
    EXPECT_FLOAT_EQ(splits.front(), 1.0f);
    EXPECT_FLOAT_EQ(splits.back(), 1000.0f);
    for (std::size_t i = 1; i < splits.size(); ++i) {
        EXPECT_GT(splits[i], splits[i - 1]);
    }
}

TEST(PracticalSplits, BlendsUniformAndLogarithmicSchemes) {
    const auto uniform = PracticalSplits(1.0f, 1001.0f, 4, 0.0f);
    EXPECT_FLOAT_EQ(uniform[1], 251.0f);
    EXPECT_FLOAT_EQ(uniform[2], 501.0f);

    const auto logarithmic = PracticalSplits(1.0f, 10000.0f, 4, 1.0f);
    EXPECT_NEAR(logarithmic[1], 10.0f, 1e-3f);
    EXPECT_NEAR(logarithmic[2], 100.0f, 1e-2f);
    EXPECT_NEAR(logarithmic[3], 1000.0f, 1e-1f);
}

TEST(FitCascades, CoversEveryFrustumSliceAndTheWholeScene) {
    const CameraView camera = TestCamera();
    const glm::vec3 toSun = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
    const auto cascades =
        FitCascades(camera, 1200.0f, toSun, kSceneMin, kSceneMax, 4, 0.8f, kResolution);
    ASSERT_EQ(cascades.size(), 4u);
    EXPECT_FLOAT_EQ(cascades.front().splitNear, camera.nearPlane);
    EXPECT_FLOAT_EQ(cascades.back().splitFar, 1200.0f);

    for (std::size_t c = 0; c < cascades.size(); ++c) {
        const ShadowCascade& cascade = cascades[c];
        if (c > 0) {
            EXPECT_FLOAT_EQ(cascade.splitNear, cascades[c - 1].splitFar);
            EXPECT_GT(cascade.texelWorldSize, cascades[c - 1].texelWorldSize);
        }
        for (const float depth : {cascade.splitNear, cascade.splitFar}) {
            for (int i = 0; i < 4; ++i) {
                const glm::vec3 ndc =
                    Project(cascade.lightViewProjection, SliceCorner(camera, depth, i));
                EXPECT_LE(std::fabs(ndc.x), 1.0f) << "cascade " << c;
                EXPECT_LE(std::fabs(ndc.y), 1.0f) << "cascade " << c;
            }
        }
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? kSceneMax.x : kSceneMin.x,
                                   (i & 2) ? kSceneMax.y : kSceneMin.y,
                                   (i & 4) ? kSceneMax.z : kSceneMin.z);
            const float depth = Project(cascade.lightViewProjection, corner).z;
            EXPECT_GE(depth, 0.0f) << "cascade " << c;
            EXPECT_LE(depth, 1.0f) << "cascade " << c;
        }
    }
}

TEST(FitCascades, SnapsToWholeTexelsSoShadowsDoNotShimmer) {
    CameraView camera = TestCamera();
    const glm::vec3 toSun = glm::normalize(glm::vec3(-0.2f, 0.4f, 0.8f));
    for (const float shift : {0.0f, 0.013f, 0.37f, 2.9f}) {
        camera.view =
            TestCamera().view * glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, -shift, shift, 0.0f, 1.0f);
        for (const ShadowCascade& cascade :
             FitCascades(camera, 1000.0f, toSun, kSceneMin, kSceneMax, 3, 0.8f, kResolution)) {
            const glm::vec3 ndc = Project(cascade.lightViewProjection, glm::vec3(0.0f));
            const float texelX = (0.5f * ndc.x + 0.5f) * kResolution;
            const float texelY = (0.5f * ndc.y + 0.5f) * kResolution;
            EXPECT_NEAR(texelX, std::round(texelX), 1e-2f);
            EXPECT_NEAR(texelY, std::round(texelY), 1e-2f);
        }
    }
}

TEST(FitCascades, HandlesAnOverheadSun) {
    const auto cascades = FitCascades(TestCamera(), 800.0f, glm::vec3(0.0f, 0.0f, 1.0f), kSceneMin,
                                      kSceneMax, 2, 0.5f, kResolution);
    ASSERT_EQ(cascades.size(), 2u);
    const glm::vec3 ndc = Project(cascades[0].lightViewProjection, glm::vec3(0.0f, 0.0f, 40.0f));
    EXPECT_TRUE(std::isfinite(ndc.x));
    EXPECT_GE(ndc.z, 0.0f);
}
