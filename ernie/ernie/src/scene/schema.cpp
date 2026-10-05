#include "ernie/scene/schema.h"

#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace ernie {

double GetNumber(const Scene& scene, const Path& path, std::string_view attribute,
                 double fallback) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return fallback;
    }
    if (const auto* d = value->Get<double>()) {
        return *d;
    }
    if (const auto* f = value->Get<float>()) {
        return *f;
    }
    if (const auto* i = value->Get<int>()) {
        return *i;
    }
    return fallback;
}

bool GetBool(const Scene& scene, const Path& path, std::string_view attribute, bool fallback) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return fallback;
    }
    if (const auto* b = value->Get<bool>()) {
        return *b;
    }
    if (const auto* i = value->Get<int>()) {
        return *i != 0;
    }
    return fallback;
}

glm::dvec2 GetVec2(const Scene& scene, const Path& path, std::string_view attribute,
                   glm::dvec2 fallback) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return fallback;
    }
    if (const auto* d = value->Get<glm::dvec2>()) {
        return *d;
    }
    if (const auto* f = value->Get<glm::vec2>()) {
        return glm::dvec2(*f);
    }
    if (const auto* i = value->Get<glm::ivec2>()) {
        return glm::dvec2(*i);
    }
    return fallback;
}

glm::dvec3 GetVec3(const Scene& scene, const Path& path, std::string_view attribute,
                   glm::dvec3 fallback) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return fallback;
    }
    if (const auto* d = value->Get<glm::dvec3>()) {
        return *d;
    }
    if (const auto* f = value->Get<glm::vec3>()) {
        return glm::dvec3(*f);
    }
    return fallback;
}

std::string GetToken(const Scene& scene, const Path& path, std::string_view attribute,
                     std::string_view fallback) {
    return scene.Get<std::string>(path, attribute, std::string(fallback));
}

Path GetTarget(const Scene& scene, const Path& path, std::string_view attribute) {
    return scene.Get<Path>(path, attribute, Path());
}

glm::dmat4 LookAtTransform(glm::dvec3 eye, glm::dvec3 target, glm::dvec3 up) {
    const glm::dvec3 back = eye - target;
    if (glm::length(back) == 0.0 || glm::length(glm::cross(up, back)) == 0.0) {
        throw std::invalid_argument("LookAtTransform needs distinct eye and target and an up "
                                    "vector off the view direction");
    }
    const glm::dvec3 z = glm::normalize(back);
    const glm::dvec3 x = glm::normalize(glm::cross(up, z));
    const glm::dvec3 y = glm::cross(z, x);
    glm::dmat4 transform(1.0);
    transform[0] = glm::dvec4(x, 0.0);
    transform[1] = glm::dvec4(y, 0.0);
    transform[2] = glm::dvec4(z, 0.0);
    transform[3] = glm::dvec4(eye, 1.0);
    return transform;
}

glm::dmat4 DistantLightTransform(glm::dvec3 towardsLight) {
    if (glm::length(towardsLight) == 0.0) {
        throw std::invalid_argument("DistantLightTransform needs a nonzero direction");
    }
    const glm::dvec3 z = glm::normalize(towardsLight);
    const glm::dvec3 helper =
        std::abs(z.z) < 0.9 ? glm::dvec3(0.0, 0.0, 1.0) : glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 x = glm::normalize(glm::cross(helper, z));
    const glm::dvec3 y = glm::cross(z, x);
    glm::dmat4 transform(1.0);
    transform[0] = glm::dvec4(x, 0.0);
    transform[1] = glm::dvec4(y, 0.0);
    transform[2] = glm::dvec4(z, 0.0);
    return transform;
}

glm::dvec3 TowardsLight(const Scene& scene, const Path& light) {
    return glm::normalize(glm::dvec3(scene.WorldTransform(light)[2]));
}

void SetCamera(Scene& scene, const Path& path, const Camera& camera) {
    using namespace tokens;
    const double aspect = static_cast<double>(camera.width) / static_cast<double>(camera.height);
    scene.Define(path, kCamera)
        .Set(kTransform, camera.transform)
        .Set(kProjection, kPerspective)
        .Set(kFocalLength, camera.focalLengthMm)
        .Set(kVerticalAperture, camera.verticalApertureMm)
        .Set(kHorizontalAperture, camera.verticalApertureMm * aspect)
        .Set(kHorizontalApertureOffset, camera.horizontalApertureOffsetMm)
        .Set(kVerticalApertureOffset, camera.verticalApertureOffsetMm)
        .Set(kClippingRange, glm::dvec2(camera.nearPlane, camera.farPlane))
        .Set(kCameraExposure, camera.exposureStops)
        .Set(kExposureTime, camera.exposureTime)
        .Set(kExposureIso, camera.iso)
        .Set(kExposureFStop, camera.fStop)
        .Set(kExposureResponsivity, camera.responsivity)
        .Set(kSpectralRange, glm::dvec2(camera.bands.minNm, camera.bands.maxNm))
        .Set(kSpectralBands, camera.bands.count);
}

}  // namespace ernie
