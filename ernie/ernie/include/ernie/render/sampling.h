#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

/// Deterministic random streams shared by every render backend.
///
/// Every random decision a renderer makes (sensor noise, and the samples of ray and path tracers)
/// derives from three numbers: a seed the scene authors, a stream naming what is rendered (a render
/// product, `StreamOf` its path) and the frame number. The same scene, settings and frame then
/// render the same image on any machine, in any order, whatever else renders alongside.
///
/// Reference: M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", Journal of Computer
/// Graphics Techniques 9(3):21-38, 2020 (pcg3d).
namespace ernie {

/// The pcg3d hash: three well-mixed 32-bit words from three. Shaders implement the same function.
inline constexpr glm::uvec3 Pcg3d(glm::uvec3 v) {
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

/// A stable 32-bit stream id for `name`, such as a render product's path: its 32-bit FNV-1a hash.
inline constexpr std::uint32_t StreamOf(std::string_view name) {
    std::uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return hash;
}

/// The seed of frame `frame` of stream `stream` under the authored `seed`, from which a frame's
/// temporal noise and samples derive. Distinct frames, streams or seeds give unrelated seeds.
inline constexpr std::uint32_t FrameSeed(std::uint32_t seed, std::uint32_t stream,
                                         std::uint64_t frame) {
    const glm::uvec3 h = Pcg3d(glm::uvec3(seed, stream ^ static_cast<std::uint32_t>(frame >> 32u),
                                          static_cast<std::uint32_t>(frame)));
    return h.x ^ h.y ^ h.z;
}

}  // namespace ernie
