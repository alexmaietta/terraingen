// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/polyhedra.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Each triangle turned to face away from the origin, which lies inside every base solid.
TriangleMesh Outward(TriangleMesh mesh) {
    for (auto& t : mesh.triangles) {
        const Vec3d& a = mesh.positions[t[0]];
        const Vec3d normal = TriangleAreaNormal(a, mesh.positions[t[1]], mesh.positions[t[2]]);
        if (Dot(normal, a + mesh.positions[t[1]] + mesh.positions[t[2]]) < 0.0) {
            std::swap(t[1], t[2]);
        }
    }
    return mesh;
}

/// Two triangles for the quadrilateral a, b, c, d, in order around it.
void AddQuad(TriangleMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c,
             std::uint32_t d) {
    mesh.triangles.push_back({a, b, c});
    mesh.triangles.push_back({a, c, d});
}

TriangleMesh IcosahedronMesh() {
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    TriangleMesh mesh;
    for (const Vec3d& v : std::initializer_list<Vec3d>{{-1, t, 0},
                                                       {1, t, 0},
                                                       {-1, -t, 0},
                                                       {1, -t, 0},
                                                       {0, -1, t},
                                                       {0, 1, t},
                                                       {0, -1, -t},
                                                       {0, 1, -t},
                                                       {t, 0, -1},
                                                       {t, 0, 1},
                                                       {-t, 0, -1},
                                                       {-t, 0, 1}}) {
        mesh.positions.push_back(Normalized(v));
    }
    mesh.triangles = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                      {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                      {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                      {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    return mesh;
}

TriangleMesh CubeMesh() {
    TriangleMesh mesh;
    for (int i = 0; i < 8; ++i) {
        mesh.positions.push_back(
            {(i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0, (i & 4) != 0 ? 1.0 : -1.0});
    }
    AddQuad(mesh, 0, 1, 3, 2);
    AddQuad(mesh, 4, 5, 7, 6);
    AddQuad(mesh, 0, 1, 5, 4);
    AddQuad(mesh, 2, 3, 7, 6);
    AddQuad(mesh, 0, 2, 6, 4);
    AddQuad(mesh, 1, 3, 7, 5);
    return mesh;
}

TriangleMesh PyramidMesh() {
    TriangleMesh mesh;
    mesh.positions = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {0, 0, 1}};
    AddQuad(mesh, 0, 1, 2, 3);
    for (std::uint32_t i = 0; i < 4; ++i) {
        mesh.triangles.push_back({i, (i + 1) % 4, 4});
    }
    return mesh;
}

TriangleMesh PrismMesh() {
    TriangleMesh mesh;
    for (const double x : {-1.0, 1.0}) {
        mesh.positions.push_back({x, -1, -1});
        mesh.positions.push_back({x, 1, -1});
        mesh.positions.push_back({x, 0, 1});
    }
    mesh.triangles = {{0, 1, 2}, {3, 4, 5}};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t j = (i + 1) % 3;
        AddQuad(mesh, i, j, j + 3, i + 3);
    }
    return mesh;
}

TriangleMesh TetrahedronMesh() {
    TriangleMesh mesh;
    mesh.positions = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
    mesh.triangles = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    return mesh;
}

}  // namespace

TriangleMesh BaseSolidMesh(BaseSolid solid) {
    switch (solid) {
    case BaseSolid::Icosahedron:
        return Outward(IcosahedronMesh());
    case BaseSolid::Cube:
        return Outward(CubeMesh());
    case BaseSolid::Pyramid:
        return Outward(PyramidMesh());
    case BaseSolid::Prism:
        return Outward(PrismMesh());
    case BaseSolid::Tetrahedron:
        return Outward(TetrahedronMesh());
    }
    throw std::invalid_argument("Unknown base solid");
}

TriangleMesh Subdivide(const TriangleMesh& mesh, int levels, bool onUnitSphere) {
    if (levels < 0) {
        throw std::invalid_argument("Subdivision levels must not be negative");
    }
    TriangleMesh result = mesh;
    for (int level = 0; level < levels; ++level) {
        const double vertices = static_cast<double>(result.positions.size()) +
                                1.5 * static_cast<double>(result.triangles.size());
        if (vertices > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::invalid_argument("Subdivision would exceed 2^32 vertices");
        }
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
        const auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
            const auto key = std::minmax(a, b);
            const auto found = midpoints.find(key);
            if (found != midpoints.end()) {
                return found->second;
            }
            Vec3d middle = (result.positions[a] + result.positions[b]) * 0.5;
            if (onUnitSphere) {
                middle = Normalized(middle);
            }
            const auto index = static_cast<std::uint32_t>(result.positions.size());
            result.positions.push_back(middle);
            midpoints.emplace(key, index);
            return index;
        };
        std::vector<std::array<std::uint32_t, 3>> triangles;
        triangles.reserve(4 * result.triangles.size());
        for (const auto& t : result.triangles) {
            const std::uint32_t ab = midpoint(t[0], t[1]);
            const std::uint32_t bc = midpoint(t[1], t[2]);
            const std::uint32_t ca = midpoint(t[2], t[0]);
            triangles.push_back({t[0], ab, ca});
            triangles.push_back({ab, t[1], bc});
            triangles.push_back({ca, bc, t[2]});
            triangles.push_back({ab, bc, ca});
        }
        result.triangles = std::move(triangles);
    }
    return result;
}

}  // namespace terraingen
