#include "render/poisson_disk.h"

#include "terraingen/kernels/hash.h"

#include <cmath>
#include <limits>

namespace terraingen::render {

std::vector<glm::vec2> PoissonDisk(std::size_t count, std::uint32_t seed,
                                   std::size_t candidatesPerPoint) {
    std::vector<glm::vec2> points;
    points.reserve(count);
    std::uint32_t counter = 0;
    const auto candidate = [&] {
        const UVec3 h = Pcg3dHash(UVec3{seed, counter++, 0x9E3779B9u});
        const float radius = std::sqrt(HashToUnitFloat(h.x));
        const float angle = 6.28318530718f * HashToUnitFloat(h.y);
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

}  // namespace terraingen::render
