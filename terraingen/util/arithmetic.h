/// @file
/// @brief Integer arithmetic helpers.
#pragma once

#include <concepts>

namespace terraingen {

/// `value` / `divisor` rounded up, for a positive `divisor`: the tiles or batches of `divisor`
/// items that cover `value` items.
template <std::unsigned_integral T> constexpr T CeilDiv(T value, T divisor) {
    return (value + divisor - 1) / divisor;
}

}  // namespace terraingen
