// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `RandomStream`, a counter-based random number stream, derived seeds and 64-bit IDs.
#pragma once

#include "terraingen/random/philox.h"
#include "terraingen/random/streams.h"

#include <cstdint>

namespace terraingen {

/// A counter-based stream of random numbers built on `Philox4x32`.
///
/// The key is (`seed`, `stream`), `stream` a tag from random/streams.h; the words `a`, `b` and `c`
/// address an independent substream, so the numbers for one cell never depend on any other cell.
/// Each substream gives 2^34 draws before it repeats.
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

    /// A uniform double in [0, 1) with 53 random bits from two draws.
    double NextDouble();

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

/// The 64-bit word with `high` above `low`: an ID from two draws or hash words.
constexpr std::uint64_t JoinWords(std::uint32_t high, std::uint32_t low) {
    return (static_cast<std::uint64_t>(high) << 32) | low;
}

/// A seed for a generator keyed by a seed alone, such as a crater population or a noise field,
/// drawn from substream (`seed`, `stream`, `a`) apart from the draws of every `RandomStream`.
/// @param stream A tag from random/streams.h.
constexpr std::uint32_t SubstreamSeed(std::uint32_t seed, std::uint32_t stream,
                                      std::uint32_t a = 0) {
    return Philox4x32({a, 0u, 0u, 0xFFFFFFFFu}, {seed, stream}).x;
}

/// The 32-bit seed for seeded field `field` of a part reseeded with `seed`
/// (`Layer::Reseeded`, `Source::Reseeded`), so one 64-bit seed varies every seed of a part.
constexpr std::uint32_t ReseedWord(std::uint64_t seed, std::uint32_t field) {
    const IdWords words = SplitId(seed);
    return Philox4x32({field, 0u, 0u, 0xFFFFFFFEu}, {words.low, words.high}).x;
}

}  // namespace terraingen
