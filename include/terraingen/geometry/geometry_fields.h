// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field list of a box.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/util/option_fields.h"

namespace terraingen {

/// The fields of a box: its lower and upper edges.
template <typename IO> void Fields(IO& io, Bounds2d& box) {
    io("min_x", box.minX, "Lower edge along x.");
    io("min_y", box.minY, "Lower edge along y.");
    io("max_x", box.maxX, "Upper edge along x; above min_x.");
    io("max_y", box.maxY, "Upper edge along y; above min_y.");
}

}  // namespace terraingen
