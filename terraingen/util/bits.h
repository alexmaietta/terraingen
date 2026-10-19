/// @file
/// @brief Mathematical constants, bit casts and byte order, in C++17.
#pragma once

#include <cstring>
#include <type_traits>

namespace terraingen {

inline constexpr double kPi = 3.141592653589793238462643383279502884;     ///< pi.
inline constexpr double kSqrt2 = 1.414213562373095048801688724209698079;  ///< sqrt(2).
/// The Euler-Mascheroni constant.
inline constexpr double kEulerGamma = 0.577215664901532860606512090082402431;

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

}  // namespace terraingen
