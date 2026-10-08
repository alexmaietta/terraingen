#pragma once

/// The prelude that lets kernel headers compile both as C++, for the CPU, and as Slang, for GPU
/// backends, so each kernel has one source (see "GPU strategy" in ARCHITECTURE.md). Kernel headers
/// are plain C++ in the subset both languages accept:
///
/// - `uint32_t`, `int32_t`, `float` and the other fixed-size types, unqualified;
/// - functional casts (`float(x)`, `uint32_t(x)`) rather than `static_cast`;
/// - plain structs with default member values, declared with `= {}` or filled in full;
/// - fixed-size arrays, and integer constants rather than `enum class`;
/// - `constexpr` and `inline` functions, which Slang compiles as ordinary functions;
/// - no references, pointers, templates or standard library; C++-only extras such as comparison
///   operators go under `#ifndef __SLANG__`.

#ifdef __SLANG__

#define constexpr
#define inline

#else

#include <stdint.h>

#endif
