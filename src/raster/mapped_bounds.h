// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `MappedBounds`, the box around a box's corners mapped by a transform.
#pragma once

#include "terraingen/util/geometry.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace terraingen {

/// The axis-aligned box around the four corners of `box` mapped by `map`.
/// @tparam Map Callable as `Vec2d(double x, double y)`.
template <typename Map> Bounds2d MappedBounds(const Bounds2d& box, const Map& map) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    Bounds2d bounds{kInf, kInf, -kInf, -kInf};
    for (const double x : {box.minX, box.maxX}) {
        for (const double y : {box.minY, box.maxY}) {
            const Vec2d corner = map(x, y);
            bounds.minX = std::fmin(bounds.minX, corner.x);
            bounds.minY = std::fmin(bounds.minY, corner.y);
            bounds.maxX = std::fmax(bounds.maxX, corner.x);
            bounds.maxY = std::fmax(bounds.maxY, corner.y);
        }
    }
    return bounds;
}

}  // namespace terraingen
