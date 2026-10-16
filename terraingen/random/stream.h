/// @file
/// @brief `RandomStream`, a counter-based random number stream, and 64-bit ID splitting.
#pragma once

#include "terraingen/random/philox.h"

#include <cstdint>

namespace terraingen {

/// A counter-based stream of random numbers built on `Philox4x32`.
///
/// The key is (`seed`, `stream`); the words `a`, `b` and `c` address an independent substream, so
/// the numbers for one cell never depend on any other cell. Each substream gives 2^34 draws before
/// it repeats.
class RandomStream {
  public:
    /// Opens a substream.
    /// @param stream Separates purposes, such as a layer or a feature kind.
    /// @param a First substream word, e.g. a cell's x.
    /// @param b Second substream word, e.g. a cell's y.
    /// @param c Third substream word, e.g. a size band.
    RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a = 0, std::uint32_t b = 0,
                 std::uint32_t c = 0);

    /// The next uniformly distributed 32-bit word.
    std::uint32_t NextU32();

    /// A uniform float in [0, 1) from the top 24 bits of one draw; every value is exactly
    /// representable.
    float NextFloat();

    /// A uniform double in [0, 1) with 53 random bits from two draws.
    double NextDouble();

    /// A uniform double in (0, 1], never zero, for logarithms.
    double NextDoubleOpenBelow();

  private:
    U32x2 key_;
    U32x4 counter_;
    U32x4 block_;
    int used_ = 4;
};

/// A 64-bit ID as two 32-bit words, for keying `RandomStream` and `Philox4x32` by it.
struct IdWords {
    std::uint32_t low = 0;   ///< Bits 0 to 31.
    std::uint32_t high = 0;  ///< Bits 32 to 63.
};

/// Splits a 64-bit ID into its low and high 32-bit words.
constexpr IdWords SplitId(std::uint64_t id) {
    return {static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32)};
}

}  // namespace terraingen
