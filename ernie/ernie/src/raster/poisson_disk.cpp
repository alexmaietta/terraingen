#include "ernie/raster/poisson_disk.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace ernie {

namespace {

// pcg3d (Jarzynski and Olano 2020).
glm::uvec3 Pcg3d(glm::uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

float UnitFloat(std::uint32_t hash) {
    return static_cast<float>(hash >> 8u) * 0x1p-24f;
}

}  // namespace

std::vector<glm::vec2> PoissonDisk(std::size_t count, std::uint32_t seed,
                                   std::size_t candidatesPerPoint) {
    std::vector<glm::vec2> points;
    points.reserve(count);
    std::uint32_t counter = 0;
    const auto candidate = [&] {
        const glm::uvec3 h = Pcg3d(glm::uvec3(seed, counter++, 0x9E3779B9u));
        const float radius = std::sqrt(UnitFloat(h.x));
        const float angle = 6.28318530718f * UnitFloat(h.y);
        return glm::vec2(radius * std::cos(angle), radius * std::sin(angle));
    };

    for (std::size_t i = 0; i < count; ++i) {
        glm::vec2 best = candidate();
        float bestDistance = -1.0f;
        const std::size_t tries = points.empty() ? 1 : candidatesPerPoint * points.size();
        for (std::size_t c = 0; c < tries; ++c) {
            const glm::vec2 p = c == 0 ? best : candidate();
            float nearest = std::numeric_limits<float>::max();
            for (const glm::vec2& q : points) {
                nearest = std::fmin(nearest, glm::length(p - q));
            }
            if (nearest > bestDistance) {
                bestDistance = nearest;
                best = p;
            }
        }
        points.push_back(best);
    }
    return points;
}

}  // namespace ernie
