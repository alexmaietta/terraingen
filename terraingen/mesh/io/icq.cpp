#include "terraingen/mesh/io/icq.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// A face's centre direction and the axes I and J run along.
struct IcqFace {
    Vec3d normal;
    Vec3d i;
    Vec3d j;
};

constexpr std::array<IcqFace, 6> kIcqFaces{{
    {{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
}};

/// The gnomonic coordinate of label `k` of `q`, exactly -1, 0 or 1 where the angle is a multiple
/// of 45 degrees, so labels shared between faces agree.
double Gnomonic(std::size_t k, std::size_t q) {
    if (k == 0) {
        return -1.0;
    }
    if (k == q) {
        return 1.0;
    }
    if (2 * k == q) {
        return 0.0;
    }
    const double angle =
        std::numbers::pi / 4.0 * (2.0 * static_cast<double>(k) / static_cast<double>(q) - 1.0);
    return std::tan(angle);
}

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
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".icq";
}

Vec3d IcqDirection(std::size_t q, std::size_t i, std::size_t j, std::size_t face) {
    if (q == 0 || i > q || j > q || face < 1 || face > 6) {
        throw std::invalid_argument("An ICQ label needs Q > 0, I and J <= Q and F from 1 to 6");
    }
    const IcqFace& f = kIcqFaces[face - 1];
    const double a = Gnomonic(i, q);
    const double b = Gnomonic(j, q);
    const Vec3d p{f.normal.x + a * f.i.x + b * f.j.x, f.normal.y + a * f.i.y + b * f.j.y,
                  f.normal.z + a * f.i.z + b * f.j.z};
    const double length = Length(p);
    return {p.x / length, p.y / length, p.z / length};
}

IcqModel ReadIcq(const std::filesystem::path& path, double metresPerUnit) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot read ICQ file " + path.string());
    }
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
        model.vertices.push_back({v.x * metresPerUnit, v.y * metresPerUnit, v.z * metresPerUnit});
    }
    return model;
}

void WriteIcq(const std::filesystem::path& path, const IcqModel& model, double metresPerUnit) {
    CheckModel(model);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot write ICQ file " + path.string());
    }
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%12zu\n", model.q);
    file << buffer.data();
    for (const Vec3d& v : model.vertices) {
        std::snprintf(buffer.data(), buffer.size(), "%15.6f%15.6f%15.6f\n", v.x / metresPerUnit,
                      v.y / metresPerUnit, v.z / metresPerUnit);
        file << buffer.data();
    }
    if (!file) {
        throw std::runtime_error("Cannot write ICQ file " + path.string());
    }
}

TriangleMesh IcqToMesh(const IcqModel& model) {
    CheckModel(model);
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

}  // namespace terraingen
