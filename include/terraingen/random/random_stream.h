#pragma once

#include "terraingen/kernels/philox.h"

#include <cstdint>

namespace terraingen {

/// A counter-based stream of random numbers built on `Philox4x32`.
///
/// The key is (`seed`, `stream`), where `stream` separates purposes (a layer, a feature kind). The
/// words `a`, `b` and `c` address an independent substream, e.g. a cell's x and y and a size band,
/// so the numbers for one cell never depend on any other cell. The last counter word counts blocks
/// of four 32-bit draws, giving each substream 2^34 draws before it repeats.
class RandomStream {
  public:
    RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a = 0, std::uint32_t b = 0,
                 std::uint32_t c = 0);

    /// Next uniformly distributed 32-bit word.
    std::uint32_t NextU32();

    /// Uniform float in [0, 1) from the top 24 bits of one draw; every value is exactly
    /// representable.
    float NextFloat();

    /// Uniform double in [0, 1) with 53 random bits from two draws.
    double NextDouble();

    /// Uniform double in (0, 1], for logarithms.
    double NextDoubleOpenBelow();

  private:
    U32x2 key_;
    U32x4 counter_;
    U32x4 block_;
    int used_ = 4;
};

}  // namespace terraingen
