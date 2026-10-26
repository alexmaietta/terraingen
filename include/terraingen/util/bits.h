// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Pi, bit casts, byte order and integer arithmetic, in C++17.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace terraingen {

inline constexpr double kPi = 3.141592653589793238462643383279502884;  ///< pi.

/// The bits of `from` read as a `To` of the same size, as `std::bit_cast` does.
template <typename To, typename From> To BitCast(const From& from) noexcept {
    static_assert(sizeof(To) == sizeof(From), "BitCast needs types of the same size");
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
                  "BitCast needs trivially copyable types");
    To to;
    std::memcpy(&to, &from, sizeof(To));
    return to;
}

/// Whether the target stores the least significant byte first.
#if defined(_MSC_VER)
inline constexpr bool kLittleEndian = true;
#else
inline constexpr bool kLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#endif

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
