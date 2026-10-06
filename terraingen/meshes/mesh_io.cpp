#include "terraingen/meshes/mesh_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

void CheckMesh(const TriangleMesh& mesh, const std::vector<Vec3d>& normals) {
    if (!normals.empty() && normals.size() != mesh.positions.size()) {
        throw std::invalid_argument("Normals must be empty or one per vertex");
    }
    for (const auto& t : mesh.triangles) {
        for (const std::uint32_t v : t) {
            if (v >= mesh.positions.size()) {
                throw std::invalid_argument("A triangle indexes past the mesh's vertices");
            }
        }
    }
}

/// Appends `value`'s bytes in little-endian order.
template <typename T> void Put(std::string& bytes, T value) {
    std::array<unsigned char, sizeof(T)> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(raw.begin(), raw.end());
    }
    bytes.append(reinterpret_cast<const char*>(raw.data()), raw.size());
}

void Save(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

std::string Number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", value);
    return buffer.data();
}

}  // namespace

void WritePly(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals) {
    CheckMesh(mesh, normals);
    std::string bytes =
        "ply\nformat binary_little_endian 1.0\ncomment TerrainGen\nelement vertex " +
        std::to_string(mesh.positions.size()) +
        "\nproperty float x\nproperty float y\nproperty float z\n";
    if (!normals.empty()) {
        bytes += "property float nx\nproperty float ny\nproperty float nz\n";
    }
    bytes += "element face " + std::to_string(mesh.triangles.size()) +
             "\nproperty list uchar uint vertex_indices\nend_header\n";
    for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
        const Vec3d& p = mesh.positions[v];
        for (const double c : {p.x, p.y, p.z}) {
            Put(bytes, static_cast<float>(c));
        }
        if (!normals.empty()) {
            for (const double c : {normals[v].x, normals[v].y, normals[v].z}) {
                Put(bytes, static_cast<float>(c));
            }
        }
    }
    for (const auto& t : mesh.triangles) {
        Put(bytes, static_cast<std::uint8_t>(3));
        for (const std::uint32_t v : t) {
            Put(bytes, v);
        }
    }
    Save(path, bytes);
}

void WriteGlb(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals) {
    CheckMesh(mesh, normals);
    if (mesh.positions.empty() || mesh.triangles.empty()) {
        throw std::invalid_argument("Cannot write an empty mesh as glTF");
    }
    const auto yUp = [](const Vec3d& p) { return Vec3d{p.x, p.z, -p.y}; };
    std::string binary;
    Vec3d low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity()};
    Vec3d high = low * -1.0;
    for (const Vec3d& position : mesh.positions) {
        const Vec3d p = yUp(position);
        const std::array<float, 3> stored{static_cast<float>(p.x), static_cast<float>(p.y),
                                          static_cast<float>(p.z)};
        for (const float c : stored) {
            Put(binary, c);
        }
        low = {std::fmin(low.x, stored[0]), std::fmin(low.y, stored[1]),
               std::fmin(low.z, stored[2])};
        high = {std::fmax(high.x, stored[0]), std::fmax(high.y, stored[1]),
                std::fmax(high.z, stored[2])};
    }
    const std::size_t positionBytes = binary.size();
    for (const Vec3d& normal : normals) {
        const Vec3d n = yUp(normal);
        for (const double c : {n.x, n.y, n.z}) {
            Put(binary, static_cast<float>(c));
        }
    }
    const std::size_t normalBytes = binary.size() - positionBytes;
    for (const auto& t : mesh.triangles) {
        for (const std::uint32_t v : t) {
            Put(binary, v);
        }
    }
    const std::size_t indexBytes = binary.size() - positionBytes - normalBytes;
    const std::string vertices = std::to_string(mesh.positions.size());
    std::string json =
        R"({"asset":{"version":"2.0","generator":"TerrainGen"},"scene":0,"scenes":[{"nodes":[0]}],)"
        R"("nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0)";
    json += normals.empty() ? "" : R"(,"NORMAL":1)";
    json += R"(},"indices":)" + std::string(normals.empty() ? "1" : "2") + R"(,"mode":4}]}],)";
    json += R"("buffers":[{"byteLength":)" + std::to_string(binary.size()) + "}],";
    json += R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" +
            std::to_string(positionBytes) + R"(,"target":34962})";
    if (!normals.empty()) {
        json += R"(,{"buffer":0,"byteOffset":)" + std::to_string(positionBytes) +
                R"(,"byteLength":)" + std::to_string(normalBytes) + R"(,"target":34962})";
    }
    json += R"(,{"buffer":0,"byteOffset":)" + std::to_string(positionBytes + normalBytes) +
            R"(,"byteLength":)" + std::to_string(indexBytes) + R"(,"target":34963}],)";
    json += R"("accessors":[{"bufferView":0,"componentType":5126,"count":)" + vertices +
            R"(,"type":"VEC3","min":[)" + Number(low.x) + "," + Number(low.y) + "," +
            Number(low.z) + R"(],"max":[)" + Number(high.x) + "," + Number(high.y) + "," +
            Number(high.z) + "]}";
    if (!normals.empty()) {
        json +=
            R"(,{"bufferView":1,"componentType":5126,"count":)" + vertices + R"(,"type":"VEC3"})";
    }
    json += R"(,{"bufferView":)" + std::string(normals.empty() ? "1" : "2") +
            R"(,"componentType":5125,"count":)" + std::to_string(3 * mesh.triangles.size()) +
            R"(,"type":"SCALAR"}]})";
    while (json.size() % 4 != 0) {
        json += ' ';
    }
    while (binary.size() % 4 != 0) {
        binary += '\0';
    }
    std::string bytes;
    Put(bytes, std::uint32_t{0x46546C67u});
    Put(bytes, std::uint32_t{2});
    Put(bytes, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
    Put(bytes, static_cast<std::uint32_t>(json.size()));
    Put(bytes, std::uint32_t{0x4E4F534Au});
    bytes += json;
    Put(bytes, static_cast<std::uint32_t>(binary.size()));
    Put(bytes, std::uint32_t{0x004E4942u});
    bytes += binary;
    Save(path, bytes);
}

}  // namespace terraingen
