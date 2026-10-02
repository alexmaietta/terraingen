#pragma once

#include "ernie/camera/orbit_camera.h"
#include "ernie/scene/scene.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>

/// Helpers for authoring and reading the schemas in `tokens.h`.
///
/// The readers accept any numeric type an author might use for an attribute (an `int`, `float` or
/// `double` number; a `vec` or `dvec` vector), since bindings such as Python's produce doubles.
namespace ernie {

/// The attribute as a number, or `fallback` if it is unset or not numeric.
double GetNumber(const Scene& scene, const Path& path, std::string_view attribute, double fallback);

/// The attribute as a bool (from a bool or an int), or `fallback`.
bool GetBool(const Scene& scene, const Path& path, std::string_view attribute, bool fallback);

/// The attribute as a 2-vector (from `vec2`, `dvec2` or `ivec2`), or `fallback`.
glm::dvec2 GetVec2(const Scene& scene, const Path& path, std::string_view attribute,
                   glm::dvec2 fallback);

/// The attribute as a 3-vector (from `vec3` or `dvec3`), or `fallback`.
glm::dvec3 GetVec3(const Scene& scene, const Path& path, std::string_view attribute,
                   glm::dvec3 fallback);

/// The attribute as a token or string, or `fallback`.
std::string GetToken(const Scene& scene, const Path& path, std::string_view attribute,
                     std::string_view fallback);

/// The attribute as a relationship target, or the empty path.
Path GetTarget(const Scene& scene, const Path& path, std::string_view attribute);

/// Camera-to-world transform of a USD camera at `eye` looking at `target`: the camera looks down
/// its -z axis with +y up, so `up` ends up in the camera's y-z plane.
/// @throws std::invalid_argument if `eye` equals `target` or `up` is parallel to the view.
glm::dmat4 LookAtTransform(glm::dvec3 eye, glm::dvec3 target, glm::dvec3 up);

/// Transform of a DistantLight shining from `towardsLight`: its +z axis points along it, so the
/// light travels down -z as UsdLux defines.
/// @throws std::invalid_argument if `towardsLight` is zero.
glm::dmat4 DistantLightTransform(glm::dvec3 towardsLight);

/// Unit vector from the scene towards a DistantLight: the +z axis of its world transform.
glm::dvec3 TowardsLight(const Scene& scene, const Path& light);

/// Sets a Camera prim's transform, vertical field of view and clipping range from `camera`, keeping
/// the USD default 50 mm focal length and fitting the vertical aperture to the field of view.
void SetCamera(Scene& scene, const Path& path, const OrbitCamera& camera);

/// The `CameraView` of the Camera prim at `path` for an image with aspect ratio `aspect`. The
/// vertical field of view is `2 atan(verticalAperture / (2 focalLength))`, so the image height
/// fits the vertical aperture and `horizontalAperture` is ignored. Unset attributes use the USD
/// defaults: focal length 50, vertical aperture 15.2908, clipping range (1, 1e6).
/// @throws std::invalid_argument if the prim is not a Camera, its projection is orthographic, or
/// its focal length, aperture or clipping range are not positive.
CameraView CameraViewOf(const Scene& scene, const Path& path, float aspect);

}  // namespace ernie
