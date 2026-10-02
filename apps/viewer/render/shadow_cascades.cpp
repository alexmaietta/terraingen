#include "render/shadow_cascades.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace terraingen::render {

namespace {

std::array<glm::vec3, 8> SliceCorners(const CameraView& camera, float nearDepth, float farDepth) {
    const glm::mat4 cameraToWorld = glm::inverse(camera.view);
    const float tanY = std::tan(0.5f * camera.verticalFov);
    const float tanX = tanY * camera.aspect;
    std::array<glm::vec3, 8> corners{};
    for (int i = 0; i < 8; ++i) {
        const float depth = (i & 4) ? farDepth : nearDepth;
        const glm::vec4 inView((i & 1 ? 1.0f : -1.0f) * tanX * depth,
                               (i & 2 ? 1.0f : -1.0f) * tanY * depth, -depth, 1.0f);
        corners[static_cast<std::size_t>(i)] = glm::vec3(cameraToWorld * inView);
    }
    return corners;
}

}  // namespace

std::vector<float> PracticalSplits(float nearPlane, float farPlane, int count, float lambda) {
    std::vector<float> splits(static_cast<std::size_t>(count) + 1);
    for (int i = 0; i <= count; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(count);
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, fraction);
        const float uniform = nearPlane + (farPlane - nearPlane) * fraction;
        splits[static_cast<std::size_t>(i)] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    splits.front() = nearPlane;
    splits.back() = farPlane;
    return splits;
}

std::vector<ShadowCascade> FitCascades(const CameraView& camera, float shadowDistance,
                                       const glm::vec3& toSun, const glm::vec3& sceneMin,
                                       const glm::vec3& sceneMax, int count, float lambda,
                                       std::uint32_t resolution) {
    const glm::vec3 up =
        std::fabs(toSun.z) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat4 lightView = glm::lookAtRH(toSun, glm::vec3(0.0f), up);

    float sceneNear = std::numeric_limits<float>::max();
    float sceneFar = std::numeric_limits<float>::lowest();
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? sceneMax.x : sceneMin.x, (i & 2) ? sceneMax.y : sceneMin.y,
                               (i & 4) ? sceneMax.z : sceneMin.z);
        const float z = (lightView * glm::vec4(corner, 1.0f)).z;
        sceneNear = std::min(sceneNear, -z);
        sceneFar = std::max(sceneFar, -z);
    }
    const float depthPadding = 0.01f * (sceneFar - sceneNear) + 1e-3f;

    const float res = static_cast<float>(resolution);
    const std::vector<float> splits = PracticalSplits(
        camera.nearPlane, std::max(shadowDistance, camera.nearPlane * 1.001f), count, lambda);
    std::vector<ShadowCascade> cascades;
    for (int c = 0; c < count; ++c) {
        ShadowCascade cascade;
        cascade.splitNear = splits[static_cast<std::size_t>(c)];
        cascade.splitFar = splits[static_cast<std::size_t>(c) + 1];
        const auto corners = SliceCorners(camera, cascade.splitNear, cascade.splitFar);

        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners) {
            center += corner / 8.0f;
        }
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = std::max(radius, glm::length(corner - center));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;
        radius *= res / (res - 2.0f);
        const float texel = 2.0f * radius / res;

        const glm::vec3 centerInLight = glm::vec3(lightView * glm::vec4(center, 1.0f));
        const float x = std::floor(centerInLight.x / texel) * texel;
        const float y = std::floor(centerInLight.y / texel) * texel;
        const glm::mat4 projection =
            glm::orthoRH_ZO(x - radius, x + radius, y - radius, y + radius,
                            sceneNear - depthPadding, sceneFar + depthPadding);
        cascade.lightViewProjection = projection * lightView;
        cascade.texelWorldSize = texel;
        cascades.push_back(cascade);
    }
    return cascades;
}

}  // namespace terraingen::render
