#include "ernie/scene/schema.h"

#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace ernie {

namespace {

constexpr double kUsdFocalLength = 50.0;
constexpr double kUsdVerticalAperture = 15.2908;

}  // namespace

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

void SetCamera(Scene& scene, const Path& path, const OrbitCamera& camera) {
    const double aperture =
        2.0 * kUsdFocalLength * std::tan(0.5 * static_cast<double>(camera.verticalFov));
    scene.Define(path, tokens::kCamera)
        .Set(tokens::kTransform,
             LookAtTransform(glm::dvec3(camera.Eye()), glm::dvec3(camera.target), {0.0, 0.0, 1.0}))
        .Set(tokens::kFocalLength, static_cast<float>(kUsdFocalLength))
        .Set(tokens::kVerticalAperture, static_cast<float>(aperture))
        .Set(tokens::kClippingRange, glm::vec2(camera.NearPlane(), camera.FarPlane()));
}

CameraView CameraViewOf(const Scene& scene, const Path& path, float aspect) {
    if (!scene.Has(path) || scene.TypeOf(path) != tokens::kCamera) {
        throw std::invalid_argument("'" + path.String() + "' is not a Camera");
    }
    if (GetToken(scene, path, tokens::kProjection, tokens::kPerspective) != tokens::kPerspective) {
        throw std::invalid_argument("Camera '" + path.String() +
                                    "': only perspective projection is supported");
    }
    const double focal = GetNumber(scene, path, tokens::kFocalLength, kUsdFocalLength);
    const double aperture = GetNumber(scene, path, tokens::kVerticalAperture, kUsdVerticalAperture);
    const glm::dvec2 clipping = GetVec2(scene, path, tokens::kClippingRange, {1.0, 1e6});
    if (!(focal > 0.0) || !(aperture > 0.0) || !(clipping.x > 0.0) || !(clipping.y > clipping.x)) {
        throw std::invalid_argument("Camera '" + path.String() +
                                    "' needs a positive focal length, aperture and clipping range");
    }
    const glm::dmat4 world = scene.WorldTransform(path);
    CameraView view;
    view.verticalFov = static_cast<float>(2.0 * std::atan(0.5 * aperture / focal));
    view.aspect = aspect;
    view.nearPlane = static_cast<float>(clipping.x);
    view.farPlane = static_cast<float>(clipping.y);
    view.eye = glm::vec3(world[3]);
    view.forward = glm::vec3(glm::normalize(-glm::dvec3(world[2])));
    view.view = glm::mat4(glm::inverse(world));
    view.projection =
        glm::perspectiveRH_ZO(view.verticalFov, aspect, view.nearPlane, view.farPlane);
    return view;
}

}  // namespace ernie
