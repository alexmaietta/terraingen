#pragma once

/// The prelude that lets kernel headers compile both as C++, for the CPU, and as Slang, for GPU
/// backends, so each kernel has one source (see "GPU strategy" in ARCHITECTURE.md). Kernel headers
/// are plain C++ in the subset both languages accept:
///
/// - `uint32_t`, `int32_t`, `int`, `float` and `bool`, with `abs`, `clamp`, `floor`, `min`, `max`,
///   `pow` and `sqrt` unqualified;
/// - functional casts (`float(x)`, `uint32_t(x)`) rather than `static_cast`;
/// - plain structs of 4-byte members with default values, built as named values
///   (`UVec3 v = {x, y, z};`), never as temporaries (`UVec3{x, y, z}`);
/// - structs passed and returned by value, since Slang has no references or pointers;
/// - fixed-size arrays sized by `static const` integers, and `enum class` with a 32-bit base;
/// - `constexpr` and `inline` functions, which Slang compiles as ordinary functions;
/// - no templates, lambdas or standard library. C++-only extras, such as comparison operators and
///   double-precision entry points, go under `#ifndef __SLANG__`.

#ifdef __SLANG__

#define constexpr
#define inline

#else

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace terraingen {
using std::abs;
using std::clamp;
using std::floor;
using std::int32_t;
using std::max;
using std::min;
using std::pow;
using std::sqrt;
using std::uint32_t;
}  // namespace terraingen

#endif
