#pragma once

#include "terraingen/noise/kernels/portable.h"

/// Hash kernels, written once for the CPU and GPU backends (`portable.h`).

namespace terraingen {

/// Three 32-bit words.
struct UVec3 {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;

#ifndef __SLANG__
    bool operator==(const UVec3&) const = default;
#endif
};

/// PCG hash of one 32-bit word: a PCG-RXS-M-XS output permutation applied to one LCG step.
///
/// Stateless and integer-only, so it gives bit-identical results on CPUs, GPUs and WebAssembly.
///
/// References:
/// - M. E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for
///   Random Number Generation", Harvey Mudd College, HMC-CS-2014-0905, 2014.
/// - M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", Journal of Computer Graphics
///   Techniques 9(3), 2020 (the `pcg` hash). https://jcgt.org/published/0009/03/02/
constexpr uint32_t PcgHash(uint32_t v) {
    const uint32_t state = v * 747796405u + 2891336453u;
    const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// Hash of three 32-bit words to three well-mixed words.
///
/// Reference: Jarzynski and Olano (2020), the `pcg3d` hash; see `PcgHash`.
constexpr UVec3 Pcg3dHash(UVec3 v) {
    v.x = v.x * 1664525u + 1013904223u;
    v.y = v.y * 1664525u + 1013904223u;
    v.z = v.z * 1664525u + 1013904223u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    v.x ^= v.x >> 16u;
    v.y ^= v.y >> 16u;
    v.z ^= v.z >> 16u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

/// Maps a hash to a float in [0, 1) using its top 24 bits, which a `float` represents exactly.
constexpr float HashToUnitFloat(uint32_t hash) {
    return float(hash >> 8u) * 0x1p-24f;
}

}  // namespace terraingen
