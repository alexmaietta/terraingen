// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/anchored_plane.h"

namespace terraingen {

AnchoredPlane::AnchoredPlane(const Surface& surface, const Vec2d& anchor)
    : surface_(surface), anchor_(anchor) {
    if (surface_.IsBody()) {
        direction_ = UnitVector(anchor_.x, anchor_.y);
    }
}

std::optional<Vec2d> AnchoredPlane::Project(double x, double y) const {
    if (!surface_.IsBody()) {
        return Vec2d{x, y};
    }
    if (Dot(UnitVector(x, y), direction_) <= 0.0) {
        return std::nullopt;
    }
    return surface_.Offset(anchor_, {x, y});
}

}  // namespace terraingen
