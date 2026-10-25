// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/ground.h"

#include <cmath>

namespace terraingen {

std::array<Vec2d, 4> GradientPoints(const Surface& surface, const Vec2d& center, double step) {
    return {surface.Locate(center, {step, 0.0}), surface.Locate(center, {-step, 0.0}),
            surface.Locate(center, {0.0, step}), surface.Locate(center, {0.0, -step})};
}

std::optional<Vec2d> GradientFromHeights(Span<const float> heights, double step) {
    const double east =
        (static_cast<double>(heights[0]) - static_cast<double>(heights[1])) / (2.0 * step);
    const double north =
        (static_cast<double>(heights[2]) - static_cast<double>(heights[3])) / (2.0 * step);
    if (!std::isfinite(east) || !std::isfinite(north)) {
        return std::nullopt;
    }
    return Vec2d{east, north};
}

std::optional<Vec2d> GroundGradient(const Ground& ground, const Surface& surface,
                                    const Vec2d& center, double step, double spacing) {
    const std::array<Vec2d, 4> points = GradientPoints(surface, center, step);
    std::array<float, 4> heights{};
    ground.HeightsAt(points, heights, spacing);
    return GradientFromHeights(heights, step);
}

void GroundAround(const Ground& ground, const Surface& surface, const Vec2d& center,
                  Span<const Vec2d> offsets, double spacing, double fallback,
                  Span<double> heights) {
    for (std::size_t k = 0; k < offsets.size(); ++k) {
        const Vec2d at = surface.Locate(center, offsets[k]);
        const float there = ground.HeightAt(at.x, at.y, spacing);
        heights[k] = std::isnan(there) ? fallback : static_cast<double>(there);
    }
}

}  // namespace terraingen
