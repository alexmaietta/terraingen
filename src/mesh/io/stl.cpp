// SPDX-License-Identifier: BSD-3-Clause

#include "mesh/io/formats.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace terraingen::detail {

namespace {

constexpr std::size_t kHeaderBytes = 80;
constexpr std::size_t kTriangleBytes = 50;

void PutVector(std::string& bytes, const Vec3d& v) {
    for (const double c : {v.x, v.y, v.z}) {
        Put(bytes, static_cast<float>(c));
    }
}

/// Collects triangle corners into a mesh, joining corners at the same position into one vertex.
class Welder {
  public:
    void AddTriangle(const std::array<Vec3d, 3>& corners) {
        std::array<std::uint32_t, 3> triangle{};
        for (std::size_t c = 0; c < 3; ++c) {
            triangle[c] = Vertex(corners[c]);
        }
        mesh_.triangles.push_back(triangle);
    }

    TriangleMesh Take() {
        return std::move(mesh_);
    }

  private:
    std::uint32_t Vertex(const Vec3d& p) {
        const auto [found, added] =
            index_.emplace(std::array<double, 3>{p.x, p.y, p.z},
                           static_cast<std::uint32_t>(mesh_.positions.size()));
        if (added) {
            mesh_.positions.push_back(p);
        }
        return found->second;
    }

    TriangleMesh mesh_;
    std::map<std::array<double, 3>, std::uint32_t> index_;
};

Vec3d GetVector(const std::string& bytes, std::size_t offset) {
    std::array<float, 3> v{};
    std::memcpy(v.data(), bytes.data() + offset, sizeof(v));
    return {static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2])};
}

TriangleMesh ReadBinary(const std::string& bytes) {
    std::uint32_t count = 0;
    std::memcpy(&count, bytes.data() + kHeaderBytes, sizeof(count));
    Welder welder;
    for (std::size_t t = 0; t < count; ++t) {
        const std::size_t start = kHeaderBytes + 4 + t * kTriangleBytes + 12;
        welder.AddTriangle(
            {GetVector(bytes, start), GetVector(bytes, start + 12), GetVector(bytes, start + 24)});
    }
    return welder.Take();
}

TriangleMesh ReadAscii(const std::string& text) {
    std::istringstream in(text);
    Welder welder;
    std::array<Vec3d, 3> corners{};
    std::size_t corner = 0;
    std::string word;
    while (in >> word) {
        if (word != "vertex") {
            continue;
        }
        Vec3d& p = corners[corner];
        if (!(in >> p.x >> p.y >> p.z)) {
            throw std::runtime_error("A malformed vertex in an ASCII STL");
        }
        if (++corner == 3) {
            welder.AddTriangle(corners);
            corner = 0;
        }
    }
    return welder.Take();
}

}  // namespace

void WriteStl(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
              const std::vector<SceneNode>& nodes) {
    std::size_t count = 0;
    for (const SceneNode& node : nodes) {
        count += meshes[node.mesh].mesh.triangles.size();
    }
    std::string bytes = "TerrainGen";
    bytes.resize(kHeaderBytes, ' ');
    Put(bytes, static_cast<std::uint32_t>(count));
    for (const SceneNode& node : nodes) {
        const TriangleMesh& mesh = meshes[node.mesh].mesh;
        for (const auto& t : mesh.triangles) {
            const std::array<Vec3d, 3> corners{Placed(node, mesh.positions[t[0]]),
                                               Placed(node, mesh.positions[t[1]]),
                                               Placed(node, mesh.positions[t[2]])};
            PutVector(bytes, Normalized(TriangleAreaNormal(corners[0], corners[1], corners[2])));
            for (const Vec3d& corner : corners) {
                PutVector(bytes, corner);
            }
            Put(bytes, static_cast<std::uint16_t>(0));
        }
    }
    WriteFile(path, bytes);
}

SceneMesh ReadStl(const std::filesystem::path& path) {
    const std::string bytes = ReadFile(path);
    SceneMesh read;
    std::uint32_t count = 0;
    if (bytes.size() >= kHeaderBytes + 4) {
        std::memcpy(&count, bytes.data() + kHeaderBytes, sizeof(count));
    }
    const bool binary =
        bytes.size() >= kHeaderBytes + 4 &&
        bytes.size() == kHeaderBytes + 4 + static_cast<std::size_t>(count) * kTriangleBytes;
    read.mesh = binary ? ReadBinary(bytes) : ReadAscii(bytes);
    return read;
}

}  // namespace terraingen::detail
