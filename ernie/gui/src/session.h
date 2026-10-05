#pragma once

#include "ernie/camera/orbit_controller.h"
#include "ernie/render/frame_params.h"
#include "ernie/scene/path.h"
#include "ernie/scene/scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ernie::gui {

/// `/__session`: the root of the prims the viewer defines for its own use.
const Path& SessionRootPath();

/// Whether `path` is the session root or under it.
bool IsSessionPath(const Path& path);

/// The Camera prims of `scene` the viewport can look through: those under the session root first,
/// then the scene's own, each in path order.
std::vector<Path> ViewableCameras(const Scene& scene);

/// Defines or updates the Camera prim at `path` with `controller`'s pose, field of view and
/// clipping range, for an image of `width` x `height`.
void AuthorControllerCamera(Scene& scene, const Path& path, const OrbitController& controller,
                            std::uint32_t width, std::uint32_t height);

/// An orbit controller reproducing the camera-to-world transform `world`: looking along its -z
/// axis at a target `distance` ahead, with `verticalFov`. Pitch is clamped to the controller's
/// range, so views from below the horizon come back looking from just above it.
OrbitController ControllerFromTransform(const glm::dmat4& world, float distance, float verticalFov);

/// The local transform that places the prim at `path` at the world transform `world`, given its
/// ancestors' transforms.
glm::dmat4 LocalFromWorld(const Scene& scene, const Path& path, const glm::dmat4& world);

/// Mirrors the RenderSettings prim `source` into the session's RenderSettings prim `session`,
/// defining it if needed: every value attribute of `source` is copied, except those in
/// `overrides`, which take the given values; attributes neither has any more are cleared. Only
/// what changed is set: `versions` remembers what was copied, so pass the same map every frame.
/// An empty `source` leaves only the overrides.
void MirrorSettings(Scene& scene, const Path& source, const Path& session,
                    const std::map<std::string, Value, std::less<>>& overrides,
                    std::map<std::string, std::uint64_t>& versions);

/// The session's camera light, `/__session/Lights/Camera`: a distant light shining along the
/// viewing direction, without shadows.
const Path& CameraLightPath();

/// The session's default light, `/__session/Lights/Default`: a white sun from above with shadows.
const Path& DefaultLightPath();

/// Defines the session lights if missing, and turns the camera light to shine along the -z axis
/// of `cameraWorld`; sets nothing that is already so.
void UpdateSessionLights(Scene& scene, const glm::dmat4& cameraWorld);

/// Whether the scene has a visible light outside the session root.
bool HasStageLights(const Scene& scene);

/// The union of `bounds`, ignoring missing ones; none if all are missing.
std::optional<Bounds3> Union(const std::vector<std::optional<Bounds3>>& bounds);

}  // namespace ernie::gui
