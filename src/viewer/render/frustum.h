#pragma once

#include <glm/glm.hpp>

#include <array>

namespace terraingen::render {

/// Six world-space clip planes of a view-projection matrix, for culling axis-aligned boxes.
///
/// Planes are stored as (a, b, c, d) with a*x + b*y + c*z + d >= 0 inside, and are not normalised,
/// which the sign tests below do not need. Clip space depth is WebGPU's [0, 1].
///
/// References:
/// - G. Gribb and K. Hartmann, "Fast Extraction of Viewing Frustum Planes from the
///   World-View-Projection Matrix", 2001.
/// - T. Akenine-Moller, E. Haines, N. Hoffman et al., "Real-Time Rendering", 4th ed., CRC Press,
///   2018, section 22.10.1 (plane/box test with the box's positive vertex).
struct Frustum {
    std::array<glm::vec4, 6> planes{};  ///< Left, right, bottom, top, near, far.

    /// Extracts the planes of `viewProjection`, which may be perspective or orthographic.
    static Frustum FromMatrix(const glm::mat4& viewProjection);

    /// False only if the box [`low`, `high`] lies entirely outside one plane. Boxes near the
    /// frustum's corners can pass without intersecting it, which is conservative for culling.
    bool Intersects(const glm::vec3& low, const glm::vec3& high) const;
};

}  // namespace terraingen::render
