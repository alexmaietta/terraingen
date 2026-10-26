// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Fractals evaluated over runs of points, octave by octave, to the bits of one point at a
/// time.
#pragma once

#include "terraingen/noise/fractals.h"
#include "terraingen/util/span.h"

#include <cstddef>

namespace terraingen {

/// `Fractal2` at each point (`xs[i]`, `ys[i]`) into `heights`, to the bit: each octave's lattice
/// turn, offset and seed are set once for every point.
/// @param params Assumed valid, as for `Fractal2`.
void Fractal2Rows(Span<const float> xs, Span<const float> ys, FractalParams params,
                  Span<float> heights);

/// `EvaluateFractals` of `count` fractals at each point (`xs[i]`, `ys[i]`) into `heights`, to the
/// bit, each fractal with `Fractal2Rows`.
void EvaluateFractalsRows(Span<const float> xs, Span<const float> ys, const Fractal* fractals,
                          std::size_t count, Span<float> heights);

}  // namespace terraingen
