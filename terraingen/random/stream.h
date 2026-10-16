/// @file
/// @brief `RandomStream`, a counter-based random number stream, and 64-bit ID splitting.
#pragma once

#include "terraingen/random/philox.h"

#include <cstdint>

namespace terraingen {

/// @brief A counter-based stream of random numbers built on `Philox4x32`.
///
/// The key is (`seed`, `stream`); the words `a`, `b` and `c` address an independent substream, so
/// the numbers for one cell never depend on any other cell. Each substream gives 2^34 draws before
/// it repeats.
class RandomStream {
  public:
    /// @brief Opens a substream.
    /// @param seed The user seed.
    /// @param stream Separates purposes, such as a layer or a feature kind.
    /// @param a First substream word, e.g. a cell's x.
    /// @param b Second substream word, e.g. a cell's y.
    /// @param c Third substream word, e.g. a size band.
    RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a = 0, std::uint32_t b = 0,
                 std::uint32_t c = 0);

    /// @brief The next uniformly distributed 32-bit word.
    /// @return The word.
    std::uint32_t NextU32();

    /// @brief A uniform float from the top 24 bits of one draw; every value is exactly
    /// representable.
    /// @return A value in [0, 1).
    float NextFloat();

    /// @brief A uniform double with 53 random bits from two draws.
    /// @return A value in [0, 1).
    double NextDouble();

    /// @brief A uniform double that is never zero, for logarithms.
    /// @return A value in (0, 1].
    double NextDoubleOpenBelow();

  private:
    U32x2 key_;
    U32x4 counter_;
    U32x4 block_;
    int used_ = 4;
};

/// @brief A 64-bit ID as two 32-bit words, for keying `RandomStream` and `Philox4x32` by it.
struct IdWords {
    std::uint32_t low = 0;   ///< Bits 0 to 31.
    std::uint32_t high = 0;  ///< Bits 32 to 63.
};

/// @brief Splits a 64-bit ID into its low and high 32-bit words.
/// @param id The ID.
/// @return Its low and high words.
constexpr IdWords SplitId(std::uint64_t id) {
    return {static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32)};
}

}  // namespace terraingen
