/// @file
/// @brief Integer arithmetic helpers.
#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace terraingen {

/// `value` / `divisor` rounded up, for a positive `divisor`: the tiles or batches of `divisor`
/// items that cover `value` items.
template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T> &&
                                                  !std::is_same_v<T, bool>>>
constexpr T CeilDiv(T value, T divisor) {
    return (value + divisor - 1) / divisor;
}

/// A whole-number `index` clamped to the valid indices [0, `size` - 1] of `size` > 0 items.
constexpr std::size_t ClampIndex(double index, std::size_t size) {
    return static_cast<std::size_t>(std::clamp(index, 0.0, static_cast<double>(size - 1)));
}

}  // namespace terraingen
