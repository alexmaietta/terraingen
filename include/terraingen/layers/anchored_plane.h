// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `AnchoredPlane`, the tangent plane at an anchor that planar layers lay features out in.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"

#include <optional>

namespace terraingen {

/// The plane a layer lays its features out in: the surface itself on a plane, or on a body the
/// tangent frame of `anchor`, true to scale near it and empty on the far hemisphere.
class AnchoredPlane {
  public:
    /// The plane of `surface`, which must outlive it, at `anchor`, in surface coordinates.
    AnchoredPlane(const Surface& surface, const Vec2d& anchor);
    AnchoredPlane(const Surface&& surface, const Vec2d& anchor) = delete;

    /// Surface (x, y) in the plane, in meters; nothing on a body's far hemisphere.
    std::optional<Vec2d> Project(double x, double y) const;

  private:
    const Surface* surface_;
    Vec2d anchor_;
    Vec3d direction_{};
};

}  // namespace terraingen
