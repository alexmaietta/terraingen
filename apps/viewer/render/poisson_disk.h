#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraingen::render {

/// Generates `count` points in the unit disk with Poisson-disk-like spacing using Mitchell's
/// best-candidate algorithm: each new point is the candidate farthest from all accepted points.
///
/// Candidates come from `PcgHash`, so the pattern is identical on every platform for a given seed.
///
/// Reference: D. P. Mitchell, "Spectrally Optimal Sampling for Distribution Ray Tracing",
/// SIGGRAPH '91, Computer Graphics 25(4):157-164, 1991. https://doi.org/10.1145/127719.122736
std::vector<glm::vec2> PoissonDisk(std::size_t count, std::uint32_t seed = 0,
                                   std::size_t candidatesPerPoint = 32);

}  // namespace terraingen::render
