// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `AnchoredPlane`, the tangent plane at an anchor that planar layers lay features out in.
#pragma once

#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <optional>

namespace terraingen {

/// The plane a layer lays its features out in: the surface itself on a plane, or on a body the
/// tangent frame of `anchor`, true to scale near it and empty on the far hemisphere.
class AnchoredPlane {
  public:
    AnchoredPlane(const Surface& surface, const Vec2d& anchor);

    /// Surface (x, y) in the plane, in meters; nothing on a body's far hemisphere.
    std::optional<Vec2d> Project(double x, double y) const;

  private:
    Surface surface_;
    Vec2d anchor_;
    Vec3d direction_{};
};

}  // namespace terraingen
