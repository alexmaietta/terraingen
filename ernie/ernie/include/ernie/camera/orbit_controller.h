#pragma once

#include "ernie/camera/camera.h"

#include <glm/glm.hpp>

namespace ernie {

/// Interactive orbit control of a `Camera` around a target point in a z-up world, for inspecting
/// terrain. It holds no camera of its own: `Apply` writes its pose, field of view and clipping
/// range into one.
///
/// Yaw rotates around the vertical axis (0 looks north, positive turns the view clockwise seen from
/// above); pitch is the elevation angle of the eye above the target's horizontal plane.
class OrbitController {
  public:
    glm::vec3 target{0.0f};
    float yaw = 0.0f;               ///< Radians.
    float pitch = 0.6f;             ///< Radians, clamped to [kMinPitch, kMaxPitch].
    float distance = 100.0f;        ///< World units from the eye to the target.
    float verticalFov = 0.785398f;  ///< Radians.

    static constexpr float kMinPitch = 0.05f;
    static constexpr float kMaxPitch = 1.55f;

    /// Eye position in world coordinates.
    glm::vec3 Eye() const;

    /// Camera-to-world transform of the eye looking at the target, +z up.
    glm::dmat4 Transform() const;

    /// Clipping planes scaled to the orbit distance.
    float NearPlane() const {
        return distance * 0.01f;
    }

    float FarPlane() const {
        return distance * 100.0f;
    }

    /// Sets `camera`'s transform, vertical field of view (through its aperture, keeping its focal
    /// length) and clipping range.
    void Apply(Camera& camera) const;

    /// A default camera of `width` x `height` with this pose, field of view and clipping range.
    Camera MakeCamera(std::uint32_t width, std::uint32_t height) const;

    /// Rotates by `deltaYaw` and `deltaPitch` radians, clamping pitch.
    void Orbit(float deltaYaw, float deltaPitch);

    /// Moves the target parallel to the ground by a screen-space drag of (`dx`, `dy`) pixels in a
    /// viewport `viewportHeight` pixels tall, so the terrain follows the cursor at the target
    /// depth.
    void Pan(float dx, float dy, float viewportHeight);

    /// Zooms by `steps` scroll steps; positive moves closer. Distance stays within [1e-3, 1e7].
    void Zoom(float steps);

    /// Aims at the centre of the axis-aligned box [`boundsMin`, `boundsMax`] from a distance at
    /// which its bounding sphere fits the vertical field of view with a small margin.
    void Frame(const glm::vec3& boundsMin, const glm::vec3& boundsMax);
};

}  // namespace ernie
