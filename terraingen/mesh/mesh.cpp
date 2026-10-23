// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/mesh.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace terraingen {

Box3d Bounds(const TriangleMesh& mesh) {
    if (mesh.positions.empty()) {
        throw std::invalid_argument("An empty mesh has no bounds");
    }
    Box3d box{mesh.positions.front(), mesh.positions.front()};
    for (const Vec3d& p : mesh.positions) {
        box.min = {std::fmin(box.min.x, p.x), std::fmin(box.min.y, p.y), std::fmin(box.min.z, p.z)};
        box.max = {std::fmax(box.max.x, p.x), std::fmax(box.max.y, p.y), std::fmax(box.max.z, p.z)};
    }
    return box;
}

namespace {

/// Twice the area-weighted normal of triangle `t`.
Vec3d AreaNormal(const TriangleMesh& mesh, const std::array<std::uint32_t, 3>& t) {
    return TriangleAreaNormal(mesh.positions[t[0]], mesh.positions[t[1]], mesh.positions[t[2]]);
}

}  // namespace

void ValidateMesh(const TriangleMesh& mesh) {
    for (const auto& t : mesh.triangles) {
        for (const std::uint32_t v : t) {
            if (v >= mesh.positions.size()) {
                throw std::invalid_argument("A triangle indexes past the mesh's vertices");
            }
        }
    }
}

std::vector<Vec3d> VertexNormals(const TriangleMesh& mesh) {
    ValidateMesh(mesh);
    std::vector<Vec3d> normals(mesh.positions.size());
    for (const auto& t : mesh.triangles) {
        const Vec3d n = AreaNormal(mesh, t);
        for (const std::uint32_t v : t) {
            normals[v] = normals[v] + n;
        }
    }
    for (Vec3d& n : normals) {
        n = Normalized(n);
    }
    return normals;
}

ShadedMesh WithCreases(const TriangleMesh& mesh, double creaseAngle) {
    RequireNonNegative(creaseAngle, "Crease angle");
    ValidateMesh(mesh);
    const double cosine = std::cos(creaseAngle);
    std::vector<Vec3d> areaNormals;
    std::vector<Vec3d> unitNormals;
    std::vector<std::vector<std::size_t>> around(mesh.positions.size());
    for (std::size_t f = 0; f < mesh.triangles.size(); ++f) {
        areaNormals.push_back(AreaNormal(mesh, mesh.triangles[f]));
        unitNormals.push_back(Normalized(areaNormals.back()));
        for (const std::uint32_t v : mesh.triangles[f]) {
            around[v].push_back(f);
        }
    }
    ShadedMesh shaded;
    std::map<std::tuple<std::uint32_t, double, double, double>, std::uint32_t> made;
    for (std::size_t f = 0; f < mesh.triangles.size(); ++f) {
        std::array<std::uint32_t, 3> triangle{};
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t v = mesh.triangles[f][k];
            Vec3d sum{};
            for (const std::size_t g : around[v]) {
                if (Dot(unitNormals[f], unitNormals[g]) >= cosine) {
                    sum = sum + areaNormals[g];
                }
            }
            const Vec3d normal = Normalized(sum);
            const auto key = std::make_tuple(v, normal.x, normal.y, normal.z);
            const auto found = made.find(key);
            if (found != made.end()) {
                triangle[k] = found->second;
                continue;
            }
            const auto index = static_cast<std::uint32_t>(shaded.mesh.positions.size());
            shaded.mesh.positions.push_back(mesh.positions[v]);
            shaded.normals.push_back(normal);
            made.emplace(key, index);
            triangle[k] = index;
        }
        shaded.mesh.triangles.push_back(triangle);
    }
    return shaded;
}

Vec3d RigidTransform::Apply(const Vec3d& p) const {
    return Rotate(p) + translation;
}

Vec3d RigidTransform::Rotate(const Vec3d& v) const {
    const auto& r = rotation;
    return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z,
            r[6] * v.x + r[7] * v.y + r[8] * v.z};
}

RigidTransform RigidTransform::After(const RigidTransform& first) const {
    RigidTransform combined;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += rotation[3 * i + k] * first.rotation[3 * k + j];
            }
            combined.rotation[3 * i + j] = sum;
        }
    }
    combined.translation = Apply(first.translation);
    return combined;
}

RigidTransform RigidTransform::RotationX(double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {{1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c}, {}};
}

RigidTransform RigidTransform::RotationY(double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {{c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c}, {}};
}

RigidTransform RigidTransform::RotationZ(double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {{c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0}, {}};
}

RigidTransform RigidTransform::Translation(const Vec3d& offset) {
    RigidTransform transform;
    transform.translation = offset;
    return transform;
}

void RequireRotation(const std::array<double, 9>& r) {
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                dot += r[3 * i + k] * r[3 * j + k];
            }
            if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > 1e-9) {
                throw std::invalid_argument("A rotation matrix must be orthonormal");
            }
        }
    }
    const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                       r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0.0) {
        throw std::invalid_argument("A rotation matrix must not be a reflection");
    }
}

void RequireScaleFactors(const Vec3d& scale, std::string_view what) {
    for (const double s : {scale.x, scale.y, scale.z}) {
        if (!(std::isfinite(s) && s != 0.0)) {
            throw std::invalid_argument(std::string(what) + " must be finite and not zero");
        }
    }
}

TriangleMesh Scaled(const TriangleMesh& mesh, const Vec3d& scale) {
    RequireScaleFactors(scale, "Scale factors");
    TriangleMesh scaled = mesh;
    for (Vec3d& p : scaled.positions) {
        p = {p.x * scale.x, p.y * scale.y, p.z * scale.z};
    }
    if (scale.x * scale.y * scale.z < 0.0) {
        for (auto& t : scaled.triangles) {
            std::swap(t[1], t[2]);
        }
    }
    return scaled;
}

void CutAtPlane(TriangleMesh& mesh, const Vec3d& normal, std::vector<double> distances) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> crossings;
    const auto crossing = [&](std::uint32_t a, std::uint32_t b) {
        const auto key = std::minmax(a, b);
        const auto found = crossings.find(key);
        if (found != crossings.end()) {
            return found->second;
        }
        const double t = distances[a] / (distances[a] - distances[b]);
        const Vec3d& pa = mesh.positions[a];
        mesh.positions.push_back(pa + (mesh.positions[b] - pa) * t);
        distances.push_back(0.0);
        const auto index = static_cast<std::uint32_t>(mesh.positions.size() - 1);
        crossings.emplace(key, index);
        return index;
    };
    std::vector<std::array<std::uint32_t, 3>> triangles;
    triangles.reserve(mesh.triangles.size());
    for (const auto& triangle : mesh.triangles) {
        const auto above = [&](std::uint32_t v) { return distances[v] > 0.0; };
        const int count = static_cast<int>(above(triangle[0])) +
                          static_cast<int>(above(triangle[1])) +
                          static_cast<int>(above(triangle[2]));
        if (count == 0 || count == 3) {
            triangles.push_back(triangle);
            continue;
        }
        std::size_t lone = 0;
        while (above(triangle[lone]) == (count == 2)) {
            ++lone;
        }
        const std::uint32_t a = triangle[lone];
        const std::uint32_t b = triangle[(lone + 1) % 3];
        const std::uint32_t c = triangle[(lone + 2) % 3];
        const std::uint32_t ab = crossing(a, b);
        const std::uint32_t ac = crossing(a, c);
        triangles.push_back({a, ab, ac});
        triangles.push_back({ab, b, c});
        triangles.push_back({ab, c, ac});
    }
    mesh.triangles = std::move(triangles);
    for (std::size_t i = 0; i < distances.size(); ++i) {
        if (distances[i] > 0.0) {
            mesh.positions[i] = mesh.positions[i] - normal * distances[i];
        }
    }
}

}  // namespace terraingen
