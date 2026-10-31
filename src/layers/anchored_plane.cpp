// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/anchored_plane.h"

namespace terraingen {

AnchoredPlane::AnchoredPlane(const Surface& surface, const Vec2d& anchor)
    : m_surface(&surface), m_anchor(anchor) {
    if (m_surface->IsBody()) {
        m_direction = UnitVector(m_anchor.x, m_anchor.y);
    }
}

std::optional<Vec2d> AnchoredPlane::Project(double x, double y) const {
    if (!m_surface->IsBody()) {
        return Vec2d{x, y};
    }
    if (Dot(UnitVector(x, y), m_direction) <= 0.0) {
        return std::nullopt;
    }
    return m_surface->Offset(m_anchor, {x, y});
}

}  // namespace terraingen
