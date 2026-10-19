/// @file
/// @brief The Philox4x32-10 counter-based generator for the CPU and GPU backends.
#pragma once

#include "terraingen/util/portable.h"

namespace terraingen {

/// Four 32-bit words: a Philox counter or output block.
struct U32x4 {
    uint32_t x = 0;  ///< First word.
    uint32_t y = 0;  ///< Second word.
    uint32_t z = 0;  ///< Third word.
    uint32_t w = 0;  ///< Fourth word.

#ifndef __SLANG__
    /// Componentwise equality.
    bool operator==(const U32x4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    bool operator!=(const U32x4& other) const {
        return !(*this == other);
    }
#endif
};

/// Two 32-bit words: a Philox key.
struct U32x2 {
    uint32_t x = 0;  ///< First word.
    uint32_t y = 0;  ///< Second word.

#ifndef __SLANG__
    /// Componentwise equality.
    bool operator==(const U32x2& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const U32x2& other) const {
        return !(*this == other);
    }
#endif
};

namespace detail {

/// The high (`x`) and low (`y`) words of the 64-bit product `a * b`, built from 16-bit halves on
/// GPUs without 64-bit integers, with the same result.
constexpr U32x2 MulHiLo32(uint32_t a, uint32_t b) {
    U32x2 result = {};
#ifdef __SLANG__
    const uint32_t aLow = a & 0xFFFFu;
    const uint32_t aHigh = a >> 16u;
    const uint32_t bLow = b & 0xFFFFu;
    const uint32_t bHigh = b >> 16u;
    const uint32_t lowHigh = aLow * bHigh;
    const uint32_t highLow = aHigh * bLow;
    const uint32_t middle = ((aLow * bLow) >> 16u) + (lowHigh & 0xFFFFu) + (highLow & 0xFFFFu);
    result.x = aHigh * bHigh + (lowHigh >> 16u) + (highLow >> 16u) + (middle >> 16u);
    result.y = a * b;
#else
    const std::uint64_t product = std::uint64_t(a) * b;
    result.x = uint32_t(product >> 32u);
    result.y = uint32_t(product);
#endif
    return result;
}

/// One Philox4x32 round of a block under a round key.
constexpr U32x4 PhiloxRound(U32x4 counter, U32x2 key) {
    const U32x2 first = MulHiLo32(0xD2511F53u, counter.x);
    const U32x2 second = MulHiLo32(0xCD9E8D57u, counter.z);
    const U32x4 result = {second.x ^ counter.y ^ key.x, second.y, first.x ^ counter.w ^ key.y,
                          first.y};
    return result;
}

}  // namespace detail

/// Philox4x32-10: 128 pseudo-random bits, a bijection of the counter under the key, so any
/// element of any stream can be computed directly. Bit-identical on every platform and GPU.
constexpr U32x4 Philox4x32(U32x4 counter, U32x2 key) {
    for (int round = 0; round < 10; ++round) {
        if (round > 0) {
            key.x += 0x9E3779B9u;
            key.y += 0xBB67AE85u;
        }
        counter = detail::PhiloxRound(counter, key);
    }
    return counter;
}

}  // namespace terraingen
