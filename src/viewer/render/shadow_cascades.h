#pragma once

#include "render/orbit_camera.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace terraingen::render {

/// One cascade of a cascaded shadow map.
struct ShadowCascade {
    glm::mat4 lightViewProjection{1.0f};  ///< World to light clip space, [0, 1] depth.
    float splitNear = 0.0f;               ///< View-space depth where this cascade starts.
    float splitFar = 0.0f;                ///< View-space depth where this cascade ends.
    float texelWorldSize = 0.0f;          ///< World size of one shadow map texel.
};

/// View-space split depths from `nearPlane` to `farPlane` with the practical split scheme, which
/// blends logarithmic splits (weight `lambda`) with uniform splits (weight 1 − `lambda`).
///
/// Reference: F. Zhang, H. Sun, L. Xu and L. K. Lun, "Parallel-Split Shadow Maps for Large-Scale
/// Virtual Environments", Proc. ACM VRCIA 2006, 311–318. https://doi.org/10.1145/1128923.1128975
///
/// @return `count + 1` increasing depths, starting at `nearPlane` and ending at `farPlane`.
std::vector<float> PracticalSplits(float nearPlane, float farPlane, int count, float lambda);

/// Fits one orthographic shadow map per split of the camera frustum up to `shadowDistance`.
///
/// Each cascade bounds its frustum slice with a sphere, so its size does not change as the camera
/// rotates, and snaps its position to whole shadow map texels, so shadow edges do not shimmer as
/// the camera moves (Valient 2008). Light-space depth spans the whole scene box, so casters outside
/// the view still cast into it.
///
/// References:
/// - W. Engel, "Cascaded Shadow Maps", in ShaderX5, Charles River Media, 2006.
/// - M. Valient, "Stable Rendering of Cascaded Shadow Maps", in ShaderX6, Charles River Media,
///   2008.
///
/// @param toSun Unit vector pointing towards the light.
std::vector<ShadowCascade> FitCascades(const CameraView& camera, float shadowDistance,
                                       const glm::vec3& toSun, const glm::vec3& sceneMin,
                                       const glm::vec3& sceneMax, int count, float lambda,
                                       std::uint32_t resolution);

}  // namespace terraingen::render
