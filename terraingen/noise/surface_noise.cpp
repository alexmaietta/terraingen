// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/surface_noise.h"

namespace terraingen {

float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y) {
    if (layers.empty()) {
        return 0.0f;
    }
    if (surface.IsBody()) {
        const Vec3d p = surface.Position({x, y});
        return EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size());
    }
    return EvaluateNoiseLayers(static_cast<float>(x - origin.x), static_cast<float>(y - origin.y),
                               layers.data(), layers.size());
}

}  // namespace terraingen
