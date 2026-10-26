// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/io/icq.h"

#include "terraingen/mesh/shape_model.h"
#include "util/text.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

void CheckModel(const IcqModel& model) {
    if (model.q == 0) {
        throw std::invalid_argument("An ICQ model needs Q > 0");
    }
    const std::size_t side = model.q + 1;
    if (model.vertices.size() != 6 * side * side) {
        throw std::invalid_argument("An ICQ model needs 6 (Q + 1)^2 vertices");
    }
    for (const Vec3d& v : model.vertices) {
        if (!(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z))) {
            throw std::invalid_argument("An ICQ model's vertices must be finite");
        }
    }
}

double CrossSquared(const Vec3d& a, const Vec3d& b) {
    const Vec3d c = Cross(a, b);
    return Dot(c, c);
}

}  // namespace

bool IsIcqPath(const std::filesystem::path& path) {
    return HasExtension(path, {".icq"});
}

IcqModel ReadIcq(const std::filesystem::path& path, double metersPerUnit) {
    std::istringstream file(ReadFile(path));
    std::string line;
    long long q = 0;
    if (!std::getline(file, line) || !(std::istringstream(line) >> q) || q <= 0 || q > 1000000) {
        throw std::runtime_error("ICQ file " + path.string() + " does not start with Q > 0");
    }
    IcqModel model;
    model.q = static_cast<std::size_t>(q);
    const std::size_t count = 6 * (model.q + 1) * (model.q + 1);
    model.vertices.reserve(count);
    std::size_t number = 1;
    while (model.vertices.size() < count) {
        if (!std::getline(file, line)) {
            throw std::runtime_error("ICQ file " + path.string() + " has " +
                                     std::to_string(model.vertices.size()) + " of " +
                                     std::to_string(count) + " vertices");
        }
        ++number;
        std::istringstream values(line);
        Vec3d v;
        if (!(values >> v.x >> v.y >> v.z) ||
            !(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z))) {
            throw std::runtime_error("ICQ file " + path.string() + " line " +
                                     std::to_string(number) + " is not three finite numbers");
        }
        model.vertices.push_back({v.x * metersPerUnit, v.y * metersPerUnit, v.z * metersPerUnit});
    }
    return model;
}

void WriteIcq(const std::filesystem::path& path, const IcqModel& model, double metersPerUnit) {
    CheckModel(model);
    std::string text;
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%12zu\n", model.q);
    text += buffer.data();
    for (const Vec3d& v : model.vertices) {
        std::snprintf(buffer.data(), buffer.size(), "%18.9f%18.9f%18.9f\n", v.x / metersPerUnit,
                      v.y / metersPerUnit, v.z / metersPerUnit);
        text += buffer.data();
    }
    WriteFile(path, text);
}

namespace {

/// For each stored vertex, the index of the one copy of it the mesh keeps: shared edge and corner
/// labels taken from one face, as Gaskell's converter does.
std::vector<std::size_t> SharedVertices(const IcqModel& model) {
    const std::size_t q = model.q;
    std::vector<std::size_t> n(model.vertices.size());
    for (std::size_t k = 0; k < n.size(); ++k) {
        n[k] = k;
    }
    const auto at = [&](std::size_t i, std::size_t j, std::size_t f) -> std::size_t& {
        return n[model.Index(i, j, f)];
    };
    for (std::size_t i = 1; i < q; ++i) {
        at(i, q, 6) = at(q - i, q, 4);
        at(i, 0, 6) = at(i, q, 2);
        at(i, 0, 5) = at(q, q - i, 1);
        at(i, 0, 4) = at(q - i, 0, 1);
        at(i, 0, 3) = at(0, i, 1);
        at(i, 0, 2) = at(i, q, 1);
    }
    for (std::size_t j = 1; j < q; ++j) {
        at(q, j, 6) = at(j, q, 5);
        at(q, j, 5) = at(0, j, 4);
        at(q, j, 4) = at(0, j, 3);
        at(q, j, 3) = at(0, j, 2);
        at(0, j, 6) = at(q - j, q, 3);
        at(0, j, 5) = at(q, j, 2);
    }
    at(0, 0, 3) = at(0, 0, 1);
    at(q, 0, 4) = at(0, 0, 1);
    at(0, 0, 2) = at(0, q, 1);
    at(q, 0, 3) = at(0, q, 1);
    at(0, 0, 4) = at(q, 0, 1);
    at(q, 0, 5) = at(q, 0, 1);
    at(0, 0, 5) = at(q, q, 1);
    at(q, 0, 2) = at(q, q, 1);
    at(0, 0, 6) = at(0, q, 2);
    at(q, q, 3) = at(0, q, 2);
    at(0, q, 5) = at(q, q, 2);
    at(q, 0, 6) = at(q, q, 2);
    at(q, q, 4) = at(0, q, 3);
    at(0, q, 6) = at(0, q, 3);
    at(q, q, 5) = at(0, q, 4);
    at(q, q, 6) = at(0, q, 4);
    return n;
}

}  // namespace

TriangleMesh IcqToMesh(const IcqModel& model) {
    CheckModel(model);
    const std::size_t q = model.q;
    const std::vector<std::size_t> shared = SharedVertices(model);
    const auto at = [&](std::size_t i, std::size_t j, std::size_t f) {
        return shared[model.Index(i, j, f)];
    };
    TriangleMesh mesh;
    std::vector<std::uint32_t> compact(model.vertices.size(), UINT32_MAX);
    const auto vertex = [&](std::size_t i, std::size_t j, std::size_t f) {
        const std::size_t k = at(i, j, f);
        if (compact[k] == UINT32_MAX) {
            compact[k] = static_cast<std::uint32_t>(mesh.positions.size());
            mesh.positions.push_back(model.vertices[k]);
        }
        return compact[k];
    };
    const auto v = [&](std::size_t i, std::size_t j, std::size_t f) {
        return model.vertices[at(i, j, f)];
    };
    mesh.triangles.reserve(12 * q * q);
    for (std::size_t f = 1; f <= 6; ++f) {
        for (std::size_t i = 0; i < q; ++i) {
            for (std::size_t j = 0; j < q; ++j) {
                const double z1 = CrossSquared(v(i, j, f), v(i + 1, j + 1, f));
                const double z2 = CrossSquared(v(i + 1, j, f), v(i, j + 1, f));
                if (z1 <= z2) {
                    mesh.triangles.push_back(
                        {vertex(i, j, f), vertex(i + 1, j + 1, f), vertex(i + 1, j, f)});
                    mesh.triangles.push_back(
                        {vertex(i, j, f), vertex(i, j + 1, f), vertex(i + 1, j + 1, f)});
                } else {
                    mesh.triangles.push_back(
                        {vertex(i, j, f), vertex(i, j + 1, f), vertex(i + 1, j, f)});
                    mesh.triangles.push_back(
                        {vertex(i + 1, j, f), vertex(i, j + 1, f), vertex(i + 1, j + 1, f)});
                }
            }
        }
    }
    return mesh;
}

RadialShape ToRadialShape(const IcqModel& model) {
    CheckModel(model);
    constexpr double kRadial = 1e-6;
    std::vector<double> radii(model.vertices.size());
    bool radial = true;
    ForEachIcqLabel(model.q, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t k) {
        const Vec3d& v = model.vertices[k];
        const double r = Length(v);
        const Vec3d d = IcqDirection(model.q, i, j, face);
        radial = radial && r > 0.0 && Length(Cross(v, d)) <= kRadial * r && Dot(v, d) >= 0.0;
        radii[k] = r;
    });
    if (!radial) {
        return ShapeModelFromMesh(IcqToMesh(model), model.q);
    }
    return RadialShape(model.q, std::move(radii));
}

IcqModel ToIcq(const RadialShape& shape) {
    IcqModel model;
    model.q = shape.Q();
    model.vertices.resize(shape.Radii().size());
    ForEachIcqLabel(model.q, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t k) {
        model.vertices[k] = IcqDirection(model.q, i, j, face) * shape.Radii()[k];
    });
    return model;
}

}  // namespace terraingen
