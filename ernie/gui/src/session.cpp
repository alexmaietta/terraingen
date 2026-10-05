#include "session.h"

#include "ernie/camera/camera.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <algorithm>
#include <cmath>

namespace ernie::gui {

const Path& SessionRootPath() {
    static const Path root{std::string(tokens::kSessionRoot)};
    return root;
}

bool IsSessionPath(const Path& path) {
    return path.HasPrefix(SessionRootPath());
}

std::vector<Path> ViewableCameras(const Scene& scene) {
    std::vector<Path> cameras = scene.Prims(tokens::kCamera);
    std::stable_partition(cameras.begin(), cameras.end(), IsSessionPath);
    return cameras;
}

void AuthorControllerCamera(Scene& scene, const Path& path, const OrbitController& controller,
                            std::uint32_t width, std::uint32_t height) {
    Camera camera = scene.Has(path) ? CameraOf(scene, path, width, height) : Camera{};
    camera.width = width;
    camera.height = height;
    controller.Apply(camera);
    SetCamera(scene, path, camera);
}

OrbitController ControllerFromTransform(const glm::dmat4& world, float distance,
                                        float verticalFov) {
    const glm::dvec3 eye(world[3]);
    const glm::dvec3 forward = glm::normalize(-glm::dvec3(world[2]));
    OrbitController controller;
    controller.verticalFov = verticalFov;
    controller.distance = distance;
    controller.yaw = static_cast<float>(std::atan2(-forward.x, forward.y));
    controller.pitch = std::clamp(static_cast<float>(std::asin(std::clamp(-forward.z, -1.0, 1.0))),
                                  OrbitController::kMinPitch, OrbitController::kMaxPitch);
    controller.target = glm::vec3(eye + forward * static_cast<double>(distance));
    return controller;
}

glm::dmat4 LocalFromWorld(const Scene& scene, const Path& path, const glm::dmat4& world) {
    const Path parent = path.Parent();
    if (parent.IsEmpty() || parent.IsRoot() || !scene.Has(parent)) {
        return world;
    }
    return glm::inverse(scene.WorldTransform(parent)) * world;
}

void MirrorSettings(Scene& scene, const Path& source, const Path& session,
                    const std::map<std::string, Value, std::less<>>& overrides,
                    std::map<std::string, std::uint64_t>& versions) {
    if (!scene.Has(session) || scene.TypeOf(session) != tokens::kRenderSettings) {
        scene.Define(session, tokens::kRenderSettings);
    }
    std::vector<std::string> kept;
    for (const auto& [attribute, value] : overrides) {
        kept.push_back(attribute);
        versions[attribute] = 0;
        const Value* current = scene.Get(session, attribute);
        if (current == nullptr || !(*current == value)) {
            scene.Set(session, attribute, value);
        }
    }
    if (!source.IsEmpty() && scene.Has(source)) {
        for (const std::string& attribute : scene.Attributes(source)) {
            const Value* value = scene.Get(source, attribute);
            if (value == nullptr || overrides.contains(attribute)) {
                continue;
            }
            kept.push_back(attribute);
            const std::uint64_t version = scene.AttributeVersion(source, attribute);
            auto [entry, inserted] = versions.try_emplace(attribute, version);
            if (inserted || entry->second != version) {
                entry->second = version;
                scene.Set(session, attribute, *value);
            }
        }
    }
    for (auto entry = versions.begin(); entry != versions.end();) {
        if (std::find(kept.begin(), kept.end(), entry->first) == kept.end()) {
            scene.Clear(session, entry->first);
            entry = versions.erase(entry);
        } else {
            ++entry;
        }
    }
}

const Path& CameraLightPath() {
    static const Path path = SessionRootPath().Child("Lights").Child("Camera");
    return path;
}

const Path& DefaultLightPath() {
    static const Path path = SessionRootPath().Child("Lights").Child("Default");
    return path;
}

namespace {

/// Sets `attribute` unless it already holds `value`.
void SetIfChanged(Scene& scene, const Path& path, std::string_view attribute, Value value) {
    const Value* current = scene.Get(path, attribute);
    if (current == nullptr || !(*current == value)) {
        scene.Set(path, attribute, std::move(value));
    }
}

}  // namespace

void UpdateSessionLights(Scene& scene, const glm::dmat4& cameraWorld) {
    if (!scene.Has(DefaultLightPath())) {
        scene.Define(DefaultLightPath(), tokens::kDistantLight)
            .Set(tokens::kTransform, DistantLightTransform({0.35, -0.5, 1.0}))
            .Set(tokens::kShadowEnable, true);
    }
    if (!scene.Has(CameraLightPath())) {
        scene.Define(CameraLightPath(), tokens::kDistantLight).Set(tokens::kShadowEnable, false);
    }
    glm::dmat4 rotation(1.0);
    for (int axis = 0; axis < 3; ++axis) {
        rotation[axis] = glm::dvec4(glm::normalize(glm::dvec3(cameraWorld[axis])), 0.0);
    }
    SetIfChanged(scene, CameraLightPath(), tokens::kTransform,
                 LocalFromWorld(scene, CameraLightPath(), rotation));
}

bool HasStageLights(const Scene& scene) {
    for (const std::string_view type :
         {tokens::kDistantLight, tokens::kSphereLight, tokens::kRectLight, tokens::kDiskLight,
          tokens::kDomeLight}) {
        for (const Path& path : scene.Prims(type)) {
            if (!IsSessionPath(path) && scene.IsVisible(path)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<Bounds3> Union(const std::vector<std::optional<Bounds3>>& bounds) {
    std::optional<Bounds3> result;
    for (const std::optional<Bounds3>& box : bounds) {
        if (!box) {
            continue;
        }
        if (!result) {
            result = box;
        } else {
            result->first = glm::min(result->first, box->first);
            result->second = glm::max(result->second, box->second);
        }
    }
    return result;
}

}  // namespace ernie::gui
