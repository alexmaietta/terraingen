#include "render/orbit_camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace terraingen::render {

namespace {

constexpr float kMinDistance = 1e-3f;
constexpr float kMaxDistance = 1e7f;

}  // namespace

glm::vec3 OrbitCamera::Eye() const {
    const float horizontal = std::cos(pitch) * distance;
    return target + glm::vec3(std::sin(yaw) * horizontal, -std::cos(yaw) * horizontal,
                              std::sin(pitch) * distance);
}

glm::mat4 OrbitCamera::View() const {
    return glm::lookAtRH(Eye(), target, glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 OrbitCamera::Projection(float aspect) const {
    return glm::perspectiveRH_ZO(verticalFov, aspect, NearPlane(), FarPlane());
}

CameraView OrbitCamera::MakeView(float aspect) const {
    CameraView view;
    view.view = View();
    view.projection = Projection(aspect);
    view.eye = Eye();
    view.forward = glm::normalize(target - view.eye);
    view.verticalFov = verticalFov;
    view.aspect = aspect;
    view.nearPlane = NearPlane();
    view.farPlane = FarPlane();
    return view;
}

void OrbitCamera::Orbit(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;
    pitch = std::clamp(pitch + deltaPitch, kMinPitch, kMaxPitch);
}

void OrbitCamera::Pan(float dx, float dy, float viewportHeight) {
    const float worldPerPixel =
        2.0f * distance * std::tan(0.5f * verticalFov) / std::max(viewportHeight, 1.0f);
    const glm::vec3 right(std::cos(yaw), std::sin(yaw), 0.0f);
    const glm::vec3 forward(-std::sin(yaw), std::cos(yaw), 0.0f);
    target += (-dx * right + dy * forward) * worldPerPixel;
}

void OrbitCamera::Zoom(float steps) {
    distance = std::clamp(distance * std::pow(0.9f, steps), kMinDistance, kMaxDistance);
}

void OrbitCamera::Frame(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    target = 0.5f * (boundsMin + boundsMax);
    const float radius = std::max(0.5f * glm::length(boundsMax - boundsMin), kMinDistance);
    distance = std::clamp(1.1f * radius / std::sin(0.5f * verticalFov), kMinDistance, kMaxDistance);
}

}  // namespace terraingen::render
