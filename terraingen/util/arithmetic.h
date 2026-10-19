/// @file
/// @brief Integer arithmetic helpers.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>

namespace terraingen {

/// `value` / `divisor` rounded up, for a positive `divisor`: the tiles or batches of `divisor`
/// items that cover `value` items.
template <std::unsigned_integral T> constexpr T CeilDiv(T value, T divisor) {
    return (value + divisor - 1) / divisor;
}

/// A whole-number `index` clamped to the valid indices [0, `size` - 1] of `size` > 0 items.
constexpr std::size_t ClampIndex(double index, std::size_t size) {
    return static_cast<std::size_t>(std::clamp(index, 0.0, static_cast<double>(size - 1)));
}

}  // namespace terraingen
