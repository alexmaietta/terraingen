// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/surface_noise.h"

#include "terraingen/noise/fractal_rows.h"

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

void FractalStackRows(const std::vector<Fractal>& fractals, const Vec2d& origin,
                      const Surface& surface, Span<const Vec2d> points, Span<float> heights) {
    if (fractals.empty() || surface.IsBody()) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            heights[i] = FractalStackAt(fractals, origin, surface, points[i].x, points[i].y);
        }
        return;
    }
    std::vector<float> xs(points.size());
    std::vector<float> ys(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        xs[i] = static_cast<float>(points[i].x - origin.x);
        ys[i] = static_cast<float>(points[i].y - origin.y);
    }
    EvaluateFractalsRows(xs, ys, fractals.data(), fractals.size(), heights);
}

}  // namespace terraingen
