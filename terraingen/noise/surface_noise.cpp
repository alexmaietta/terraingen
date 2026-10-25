// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/surface_noise.h"

namespace terraingen {

float FractalStackAt(const std::vector<Fractal>& fractals, const Vec2d& origin,
                     const Surface& surface, double x, double y) {
    if (fractals.empty()) {
        return 0.0f;
    }
    if (surface.IsBody()) {
        const Vec3d p = surface.Position({x, y});
        return EvaluateFractals3(p.x, p.y, p.z, fractals.data(), fractals.size());
    }
    return EvaluateFractals(static_cast<float>(x - origin.x), static_cast<float>(y - origin.y),
                            fractals.data(), fractals.size());
}

}  // namespace terraingen
