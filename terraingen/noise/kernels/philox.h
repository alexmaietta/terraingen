#pragma once

#include "terraingen/noise/kernels/portable.h"

namespace terraingen {

/// Four 32-bit words: a Philox counter or output block.
struct U32x4 {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint32_t w = 0;

#ifndef __SLANG__
    bool operator==(const U32x4&) const = default;
#endif
};

/// Two 32-bit words: a Philox key.
struct U32x2 {
    uint32_t x = 0;
    uint32_t y = 0;

#ifndef __SLANG__
    bool operator==(const U32x2&) const = default;
#endif
};

namespace detail {

/// The high (`x`) and low (`y`) words of the 64-bit product of `a` and `b`. GPUs without 64-bit
/// integers get it from 16-bit halves, with the same result.
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

constexpr U32x4 PhiloxRound(U32x4 counter, U32x2 key) {
    const U32x2 first = MulHiLo32(0xD2511F53u, counter.x);
    const U32x2 second = MulHiLo32(0xCD9E8D57u, counter.z);
    const U32x4 result = {second.x ^ counter.y ^ key.x, second.y, first.x ^ counter.w ^ key.y,
                          first.y};
    return result;
}

}  // namespace detail

/// Philox4x32-10: maps a 128-bit `counter` and 64-bit `key` to 128 pseudo-random bits.
///
/// A counter-based generator: every output is a bijection of its counter under the key, so any
/// element of any stream can be computed directly, in any order, on any thread or tile. Passes
/// TestU01 BigCrush. Integer-only, so results are bit-identical on every platform and GPU.
///
/// Reference: J. K. Salmon, M. A. Moraes, R. O. Dror and D. E. Shaw, "Parallel Random Numbers: As
/// Easy as 1, 2, 3", Proc. SC '11, 2011. https://doi.org/10.1145/2063384.2063405 (Random123).
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
