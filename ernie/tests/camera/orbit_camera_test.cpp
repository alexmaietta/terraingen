#include "ernie/camera/orbit_camera.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>

using ernie::OrbitCamera;

namespace {

glm::vec3 Project(const glm::mat4& viewProjection, const glm::vec3& point) {
    const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
    return glm::vec3(clip) / clip.w;
}

}  // namespace

TEST(OrbitCamera, EyeSitsAtDistanceAndPitchAboveTarget) {
    OrbitCamera camera;
    camera.target = {10.0f, 20.0f, 5.0f};
    camera.distance = 50.0f;
    camera.pitch = 0.5f;
    const glm::vec3 offset = camera.Eye() - camera.target;
    EXPECT_NEAR(glm::length(offset), 50.0f, 1e-3f);
    EXPECT_NEAR(offset.z, 50.0f * std::sin(0.5f), 1e-3f);
}

TEST(OrbitCamera, ZeroYawLooksNorth) {
    OrbitCamera camera;
    const glm::vec3 offset = camera.Eye() - camera.target;
    EXPECT_NEAR(offset.x, 0.0f, 1e-4f);
    EXPECT_LT(offset.y, 0.0f);
}

TEST(OrbitCamera, ViewPlacesTargetStraightAhead) {
    OrbitCamera camera;
    camera.target = {3.0f, -4.0f, 1.0f};
    camera.distance = 25.0f;
    camera.yaw = 1.2f;
    const glm::vec4 inView = camera.View() * glm::vec4(camera.target, 1.0f);
    EXPECT_NEAR(inView.x, 0.0f, 1e-3f);
    EXPECT_NEAR(inView.y, 0.0f, 1e-3f);
    EXPECT_NEAR(inView.z, -25.0f, 1e-3f);
}

TEST(OrbitCamera, ProjectionUsesZeroToOneDepth) {
    OrbitCamera camera;
    camera.distance = 10.0f;
    const glm::mat4 viewProjection = camera.ViewProjection(1.0f);
    const glm::vec3 forward = glm::normalize(camera.target - camera.Eye());
    EXPECT_NEAR(Project(viewProjection, camera.Eye() + forward * 0.1f).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(viewProjection, camera.Eye() + forward * 1000.0f).z, 1.0f, 1e-4f);
}

TEST(OrbitCamera, OrbitClampsPitch) {
    OrbitCamera camera;
    camera.Orbit(0.3f, 10.0f);
    EXPECT_FLOAT_EQ(camera.pitch, OrbitCamera::kMaxPitch);
    EXPECT_FLOAT_EQ(camera.yaw, 0.3f);
    camera.Orbit(0.0f, -10.0f);
    EXPECT_FLOAT_EQ(camera.pitch, OrbitCamera::kMinPitch);
}

TEST(OrbitCamera, ZoomMovesCloserAndClamps) {
    OrbitCamera camera;
    camera.distance = 100.0f;
    camera.Zoom(1.0f);
    EXPECT_FLOAT_EQ(camera.distance, 90.0f);
    camera.Zoom(-1.0f);
    EXPECT_NEAR(camera.distance, 100.0f, 1e-3f);
    camera.Zoom(1000.0f);
    EXPECT_FLOAT_EQ(camera.distance, 1e-3f);
}

TEST(OrbitCamera, PanFollowsTheCursorAtTargetDepth) {
    OrbitCamera camera;
    camera.distance = 100.0f;
    camera.pitch = OrbitCamera::kMaxPitch;
    const float viewportHeight = 500.0f;
    const glm::vec3 before = camera.target;
    camera.Pan(50.0f, 0.0f, viewportHeight);
    const float expected =
        50.0f * 2.0f * 100.0f * std::tan(0.5f * camera.verticalFov) / viewportHeight;
    EXPECT_NEAR(glm::length(camera.target - before), expected, 1e-3f);
    EXPECT_LT(camera.target.x, before.x);
    EXPECT_FLOAT_EQ(camera.target.z, before.z);
}

TEST(OrbitCamera, FrameFitsTheBoxInView) {
    OrbitCamera camera;
    camera.yaw = 0.7f;
    const glm::vec3 boundsMin(-500.0f, -300.0f, -40.0f);
    const glm::vec3 boundsMax(500.0f, 300.0f, 60.0f);
    camera.Frame(boundsMin, boundsMax);
    EXPECT_NEAR(camera.target.z, 10.0f, 1e-3f);

    const glm::mat4 viewProjection = camera.ViewProjection(1.0f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? boundsMax.x : boundsMin.x,
                               (i & 2) ? boundsMax.y : boundsMin.y,
                               (i & 4) ? boundsMax.z : boundsMin.z);
        const glm::vec3 ndc = Project(viewProjection, corner);
        EXPECT_LE(std::fabs(ndc.x), 1.0f) << i;
        EXPECT_LE(std::fabs(ndc.y), 1.0f) << i;
        EXPECT_GE(ndc.z, 0.0f) << i;
        EXPECT_LE(ndc.z, 1.0f) << i;
    }
}
