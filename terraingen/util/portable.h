/// @file
/// @brief Prelude that compiles kernel headers both as C++ (CPU) and as Slang (GPU).
///
/// Each kernel has one source (see "GPU strategy" in ARCHITECTURE.md). Kernel headers are plain C++
/// in the subset both languages accept:
///
/// - `uint32_t`, `int32_t`, `int`, `float` and `bool`, with `abs`, `atan2`, `clamp`, `cos`, `exp`,
///   `floor`, `log`, `min`, `max`, `pow`, `sin` and `sqrt` unqualified;
/// - functional casts (`float(x)`, `uint32_t(x)`) rather than `static_cast`;
/// - plain structs of 4-byte members with default values, built as named values
///   (`UVec3 v = {x, y, z};`), never as temporaries (`UVec3{x, y, z}`);
/// - structs returned by value, and passed by value or as `KERNEL_IN(T)`, a reference in C++ only,
///   since Slang has no references or pointers;
/// - fixed-size arrays sized by `static const` integers, and `enum class` with a 32-bit base;
/// - `constexpr` and `inline` functions, which Slang compiles as ordinary functions;
/// - no templates, lambdas or standard library. C++-only extras, such as comparison operators and
///   double-precision entry points, go under `#ifndef __SLANG__`.
#pragma once

#ifdef __SLANG__

#define constexpr
#define inline
#define KERNEL_IN(type) type

#else

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

/// A kernel parameter read but not changed: by value in Slang, which has no references, and by
/// reference in C++, so large structs are not copied per sample.
#define KERNEL_IN(type) const type&

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
