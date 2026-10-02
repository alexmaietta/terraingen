#include "ernie/camera/orbit_camera.h"
#include "ernie/geometry/frustum.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using ernie::Frustum;
using ernie::OrbitCamera;

namespace {

bool Contains(const Frustum& frustum, const glm::vec3& centre, float halfSize) {
    return frustum.Intersects(centre - glm::vec3(halfSize), centre + glm::vec3(halfSize));
}

}  // namespace

TEST(Frustum, PerspectiveKeepsTheTargetAndCullsBoxesOutsideEachPlane) {
    OrbitCamera camera;
    camera.target = {10.0f, 20.0f, 0.0f};
    camera.distance = 100.0f;
    const Frustum frustum = Frustum::FromMatrix(camera.MakeView(1.5f).ViewProjection());
    const glm::vec3 eye = camera.Eye();
    const glm::vec3 forward = glm::normalize(camera.target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f)));
    const glm::vec3 up = glm::cross(right, forward);

    EXPECT_TRUE(Contains(frustum, camera.target, 1.0f));
    EXPECT_FALSE(Contains(frustum, eye - 50.0f * forward, 1.0f)) << "behind the camera";
    EXPECT_FALSE(Contains(frustum, eye + 0.2f * forward, 0.1f)) << "before the near plane";
    EXPECT_FALSE(Contains(frustum, eye + 20000.0f * forward, 1.0f)) << "past the far plane";
    EXPECT_FALSE(Contains(frustum, camera.target + 500.0f * right, 1.0f));
    EXPECT_FALSE(Contains(frustum, camera.target - 500.0f * right, 1.0f));
    EXPECT_FALSE(Contains(frustum, camera.target + 500.0f * up, 1.0f));
    EXPECT_FALSE(Contains(frustum, camera.target - 500.0f * up, 1.0f));
    EXPECT_TRUE(Contains(frustum, camera.target + 500.0f * right, 480.0f)) << "straddles a plane";
}

TEST(Frustum, OrthographicPlanesMatchTheBoxWithZeroToOneDepth) {
    const glm::mat4 projection = glm::orthoRH_ZO(-10.0f, 10.0f, -5.0f, 5.0f, 1.0f, 50.0f);
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const Frustum frustum = Frustum::FromMatrix(projection * view);

    EXPECT_TRUE(Contains(frustum, glm::vec3(0.0f, 0.0f, -25.0f), 0.5f));
    EXPECT_TRUE(Contains(frustum, glm::vec3(9.9f, 4.9f, -1.1f), 0.05f));
    EXPECT_FALSE(Contains(frustum, glm::vec3(10.2f, 0.0f, -25.0f), 0.1f));
    EXPECT_FALSE(Contains(frustum, glm::vec3(0.0f, -5.2f, -25.0f), 0.1f));
    EXPECT_FALSE(Contains(frustum, glm::vec3(0.0f, 0.0f, -0.5f), 0.1f));
    EXPECT_FALSE(Contains(frustum, glm::vec3(0.0f, 0.0f, -50.5f), 0.1f));
}
