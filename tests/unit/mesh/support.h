#pragma once

#include "terraingen/mesh/mesh.h"

#include <map>
#include <utility>

namespace terraingen::testing {

/// Whether every edge of `mesh` is shared by exactly two triangles that run along it in opposite
/// directions: the mesh is closed and consistently oriented.
inline bool IsClosedAndOriented(const TriangleMesh& mesh) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& t : mesh.triangles) {
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[{t[k], t[(k + 1) % 3]}];
        }
    }
    for (const auto& [edge, count] : edges) {
        const auto opposite = edges.find({edge.second, edge.first});
        if (count != 1 || opposite == edges.end() || opposite->second != 1) {
            return false;
        }
    }
    return true;
}

/// Whether every triangle of `mesh` faces away from the origin, which a mesh star-shaped about the
/// origin does: no part of it folds over another.
inline bool FacesAwayFromOrigin(const TriangleMesh& mesh) {
    for (const auto& t : mesh.triangles) {
        const Vec3d& a = mesh.positions[t[0]];
        const Vec3d& b = mesh.positions[t[1]];
        const Vec3d& c = mesh.positions[t[2]];
        if (!(Dot(a, Cross(b, c)) > 0.0)) {
            return false;
        }
    }
    return true;
}

}  // namespace terraingen::testing
