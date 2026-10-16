/// @file
/// @brief Patch noise and noise-layer stacks read on a plane or a sphere.
#include "terraingen/layers/surface_noise.h"

#include "terraingen/noise/kernels/perlin.h"

#include <algorithm>

namespace terraingen {

double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed) {
    double coarse = 0.0;
    double fine = 0.0;
    if (surface.IsSphere()) {
        const Vec3d p = surface.Position({x, y});
        const double s = 1.0 / size;
        coarse = static_cast<double>(PerlinNoise3Precise(s * p.x, s * p.y, s * p.z, seed));
        fine = static_cast<double>(
            PerlinNoise3Precise(4.0 * s * p.x, 4.0 * s * p.y, 4.0 * s * p.z, seed + 1u));
    } else {
        const auto px = static_cast<float>(x / size);
        const auto py = static_cast<float>(y / size);
        coarse = static_cast<double>(PerlinNoise2(px, py, seed));
        fine = static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, seed + 1u));
    }
    return std::clamp(2.5 * (0.7 * coarse + 0.3 * fine), -1.0, 1.0);
}

float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y) {
    if (surface.IsSphere()) {
        const Vec3d p = surface.Position({x, y});
        return EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size());
    }
    return EvaluateNoiseLayers(static_cast<float>(x - origin.x), static_cast<float>(y - origin.y),
                               layers.data(), layers.size());
}

}  // namespace terraingen
