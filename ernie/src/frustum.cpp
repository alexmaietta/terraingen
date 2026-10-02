#include "ernie/frustum.h"

namespace ernie {

Frustum Frustum::FromMatrix(const glm::mat4& viewProjection) {
    // glm is column-major, so row i is (m[0][i], m[1][i], m[2][i], m[3][i]).
    const glm::mat4 m = glm::transpose(viewProjection);
    Frustum frustum;
    frustum.planes[0] = m[3] + m[0];
    frustum.planes[1] = m[3] - m[0];
    frustum.planes[2] = m[3] + m[1];
    frustum.planes[3] = m[3] - m[1];
    // With [0, 1] depth the near plane is z_clip >= 0 rather than Gribb and Hartmann's
    // z_clip >= -w_clip.
    frustum.planes[4] = m[2];
    frustum.planes[5] = m[3] - m[2];
    return frustum;
}

bool Frustum::Intersects(const glm::vec3& low, const glm::vec3& high) const {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 positive(plane.x >= 0.0f ? high.x : low.x, plane.y >= 0.0f ? high.y : low.y,
                                 plane.z >= 0.0f ? high.z : low.z);
        if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

}  // namespace ernie
