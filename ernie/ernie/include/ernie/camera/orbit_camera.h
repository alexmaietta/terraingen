#pragma once

#include <glm/glm.hpp>

namespace ernie {

/// Everything a renderer needs to know about a perspective camera for one frame.
struct CameraView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 forward{0.0f, 1.0f, 0.0f};  ///< Unit viewing direction.
    float verticalFov = 0.785398f;        ///< Radians.
    float aspect = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 ViewProjection() const {
        return projection * view;
    }
};

/// Camera orbiting a target point in a z-up world, for inspecting terrain.
///
/// Yaw rotates around the vertical axis (0 looks north, positive turns the view clockwise seen from
/// above); pitch is the elevation angle of the camera above the target's horizontal plane. The
/// projection uses WebGPU's [0, 1] depth range.
class OrbitCamera {
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

    glm::mat4 View() const;

    /// Perspective projection with near and far planes scaled to the orbit distance.
    glm::mat4 Projection(float aspect) const;

    glm::mat4 ViewProjection(float aspect) const {
        return Projection(aspect) * View();
    }

    float NearPlane() const {
        return distance * 0.01f;
    }

    float FarPlane() const {
        return distance * 100.0f;
    }

    /// Snapshot of this camera for a viewport with the given aspect ratio.
    CameraView MakeView(float aspect) const;

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
