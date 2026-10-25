// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Fractal stacks evaluated at points of a plane or a body's surface.
#pragma once

#include "terraingen/noise/fractals.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/span.h"
#include "terraingen/util/surface.h"

#include <vector>

namespace terraingen {

/// The summed noise of `fractals` at surface (x, y): 2D relative to `origin` on a plane (keeping
/// single precision accurate), 3D at the point's 3D position on a body; 0 for no fractals.
/// @param origin Where plane coordinates are measured from; unused on a body.
float FractalStackAt(const std::vector<Fractal>& fractals, const Vec2d& origin,
                     const Surface& surface, double x, double y);

/// `FractalStackAt` at each of `points` into `heights`, to the bit: on a plane a run of points at
/// a time (`EvaluateFractalsRows`).
void FractalStackRows(const std::vector<Fractal>& fractals, const Vec2d& origin,
                      const Surface& surface, Span<const Vec2d> points, Span<float> heights);

}  // namespace terraingen
