// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Prelude that compiles kernel headers, written in the subset of C++ that Slang accepts,
/// both as C++ (CPU) and as Slang (GPU); `KERNEL_IN(T)` passes a struct by reference in C++ only.
#pragma once

#ifdef __SLANG__

#define constexpr
#define inline
#define KERNEL_IN(type) type
#define TERRAINGEN_KERNEL_INLINE

#else

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

/// A kernel parameter read but not changed: by value in Slang, which has no references, and by
/// reference in C++, so large structs are not copied per sample.
#define KERNEL_IN(type) const type&

/// Inlines a kernel function into its callers, such as the loops over runs of points that GCC and
/// Clang vectorize; a plain `inline` elsewhere.
#if defined(__GNUC__) || defined(__clang__)
#define TERRAINGEN_KERNEL_INLINE inline __attribute__((always_inline))
#else
#define TERRAINGEN_KERNEL_INLINE inline
#endif

namespace terraingen {
using std::abs;
using std::atan2;
using std::clamp;
using std::cos;
using std::exp;
using std::floor;
using std::int32_t;
using std::log;
using std::max;
using std::min;
using std::pow;
using std::sin;
using std::sqrt;
using std::uint32_t;
}  // namespace terraingen

#endif
