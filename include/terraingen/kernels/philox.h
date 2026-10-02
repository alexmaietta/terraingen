#pragma once

#include <cstdint>

namespace terraingen {

/// Four 32-bit words: a Philox counter or output block.
struct U32x4 {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
    std::uint32_t w = 0;

    bool operator==(const U32x4&) const = default;
};

/// Two 32-bit words: a Philox key.
struct U32x2 {
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    bool operator==(const U32x2&) const = default;
};

namespace detail {

constexpr void MulHiLo32(std::uint32_t a, std::uint32_t b, std::uint32_t& hi, std::uint32_t& lo) {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * b;
    hi = static_cast<std::uint32_t>(product >> 32u);
    lo = static_cast<std::uint32_t>(product);
}

constexpr U32x4 PhiloxRound(U32x4 counter, U32x2 key) {
    std::uint32_t hi0 = 0;
    std::uint32_t lo0 = 0;
    std::uint32_t hi1 = 0;
    std::uint32_t lo1 = 0;
    MulHiLo32(0xD2511F53u, counter.x, hi0, lo0);
    MulHiLo32(0xCD9E8D57u, counter.z, hi1, lo1);
    return U32x4{hi1 ^ counter.y ^ key.x, lo1, hi0 ^ counter.w ^ key.y, lo0};
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
