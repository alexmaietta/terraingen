#include "ernie/camera/orbit_controller.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>

using ernie::OrbitController;

namespace {

glm::vec3 Project(const glm::mat4& viewProjection, const glm::vec3& point) {
    const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
    return glm::vec3(clip) / clip.w;
}

}  // namespace

TEST(OrbitController, EyeSitsAtDistanceAndPitchAboveTarget) {
    OrbitController camera;
    camera.target = {10.0f, 20.0f, 5.0f};
    camera.distance = 50.0f;
    camera.pitch = 0.5f;
    const glm::vec3 offset = camera.Eye() - camera.target;
    EXPECT_NEAR(glm::length(offset), 50.0f, 1e-3f);
    EXPECT_NEAR(offset.z, 50.0f * std::sin(0.5f), 1e-3f);
}

TEST(OrbitController, ZeroYawLooksNorth) {
    OrbitController camera;
    const glm::vec3 offset = camera.Eye() - camera.target;
    EXPECT_NEAR(offset.x, 0.0f, 1e-4f);
    EXPECT_LT(offset.y, 0.0f);
}

TEST(OrbitController, ViewPlacesTargetStraightAhead) {
    OrbitController camera;
    camera.target = {3.0f, -4.0f, 1.0f};
    camera.distance = 25.0f;
    camera.yaw = 1.2f;
    const glm::vec4 inView =
        glm::mat4(glm::inverse(camera.Transform())) * glm::vec4(camera.target, 1.0f);
    EXPECT_NEAR(inView.x, 0.0f, 1e-3f);
    EXPECT_NEAR(inView.y, 0.0f, 1e-3f);
    EXPECT_NEAR(inView.z, -25.0f, 1e-3f);
}

TEST(OrbitController, ProjectionUsesZeroToOneDepth) {
    OrbitController camera;
    camera.distance = 10.0f;
    const glm::mat4 viewProjection = camera.MakeCamera(1, 1).RasterView().ViewProjection();
    const glm::vec3 forward = glm::normalize(camera.target - camera.Eye());
    EXPECT_NEAR(Project(viewProjection, camera.Eye() + forward * 0.1f).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(viewProjection, camera.Eye() + forward * 1000.0f).z, 1.0f, 1e-4f);
}

TEST(OrbitController, OrbitClampsPitch) {
    OrbitController camera;
    camera.Orbit(0.3f, 10.0f);
    EXPECT_FLOAT_EQ(camera.pitch, OrbitController::kMaxPitch);
    EXPECT_FLOAT_EQ(camera.yaw, 0.3f);
    camera.Orbit(0.0f, -10.0f);
    EXPECT_FLOAT_EQ(camera.pitch, OrbitController::kMinPitch);
}

TEST(OrbitController, ZoomMovesCloserAndClamps) {
    OrbitController camera;
    camera.distance = 100.0f;
    camera.Zoom(1.0f);
    EXPECT_FLOAT_EQ(camera.distance, 90.0f);
    camera.Zoom(-1.0f);
    EXPECT_NEAR(camera.distance, 100.0f, 1e-3f);
    camera.Zoom(1000.0f);
    EXPECT_FLOAT_EQ(camera.distance, 1e-3f);
}

TEST(OrbitController, PanFollowsTheCursorAtTargetDepth) {
    OrbitController camera;
    camera.distance = 100.0f;
    camera.pitch = OrbitController::kMaxPitch;
    const float viewportHeight = 500.0f;
    const glm::vec3 before = camera.target;
    camera.Pan(50.0f, 0.0f, viewportHeight);
    const float expected =
        50.0f * 2.0f * 100.0f * std::tan(0.5f * camera.verticalFov) / viewportHeight;
    EXPECT_NEAR(glm::length(camera.target - before), expected, 1e-3f);
    EXPECT_LT(camera.target.x, before.x);
    EXPECT_FLOAT_EQ(camera.target.z, before.z);
}

TEST(OrbitController, FrameFitsTheBoxInView) {
    OrbitController camera;
    camera.yaw = 0.7f;
    const glm::vec3 boundsMin(-500.0f, -300.0f, -40.0f);
    const glm::vec3 boundsMax(500.0f, 300.0f, 60.0f);
    camera.Frame(boundsMin, boundsMax);
    EXPECT_NEAR(camera.target.z, 10.0f, 1e-3f);

    const glm::mat4 viewProjection = camera.MakeCamera(1, 1).RasterView().ViewProjection();
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

TEST(OrbitController, AppliesItsPoseFieldOfViewAndClippingToACamera) {
    OrbitController orbit;
    orbit.target = {1.0f, 2.0f, 3.0f};
    orbit.distance = 40.0f;
    orbit.verticalFov = 0.5f;
    ernie::Camera camera;
    camera.focalLengthMm = 35.0;
    camera.exposureStops = 2.0;
    orbit.Apply(camera);
    EXPECT_NEAR(camera.VerticalFov(), 0.5, 1e-6);
    EXPECT_DOUBLE_EQ(camera.focalLengthMm, 35.0) << "the focal length is kept";
    EXPECT_DOUBLE_EQ(camera.exposureStops, 2.0) << "and so is everything else";
    EXPECT_NEAR(camera.nearPlane, 0.4, 1e-6);
    EXPECT_NEAR(camera.farPlane, 4000.0, 1e-3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(camera.Eye()[i], orbit.Eye()[i], 1e-4);
    }
    const glm::dvec3 forward = glm::normalize(glm::dvec3(orbit.target - orbit.Eye()));
    EXPECT_NEAR(glm::dot(camera.Forward(), forward), 1.0, 1e-6);
}
