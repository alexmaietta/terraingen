/// @file
/// @brief Stateless PCG hashes for the CPU and GPU backends (`portable.h`).
#pragma once

#include "terraingen/util/portable.h"

namespace terraingen {

/// Three 32-bit words.
struct UVec3 {
    uint32_t x = 0;  ///< First word.
    uint32_t y = 0;  ///< Second word.
    uint32_t z = 0;  ///< Third word.

#ifndef __SLANG__
    bool operator==(const UVec3&) const = default;
#endif
};

/// PCG hash (PCG-RXS-M-XS over one LCG step) of one 32-bit word; bit-identical everywhere.
constexpr uint32_t PcgHash(uint32_t v) {
    const uint32_t state = v * 747796405u + 2891336453u;
    const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// The `pcg3d` hash of three 32-bit words to three well-mixed words.
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

/// Maps a hash to a float in [0, 1) from its top 24 bits, which a `float` represents exactly.
constexpr float HashToUnitFloat(uint32_t hash) {
    return float(hash >> 8u) * 0x1p-24f;
}

}  // namespace terraingen
