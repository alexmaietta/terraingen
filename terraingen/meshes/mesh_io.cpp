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
    std::snprintf(buffer.data(), buffer.size(), "%.9g", value == 0.0 ? 0.0 : value);
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

namespace {

/// glTF is y-up: a z-up point (x, y, z) is written as (x, z, -y).
Vec3d YUp(const Vec3d& p) {
    return {p.x, p.z, -p.y};
}

/// The unit quaternion (x, y, z, w) of rotation matrix `r` (row-major).
///
/// Reference: S. W. Shepperd, "Quaternion from rotation matrix", Journal of Guidance and Control
/// 1(3):223-224, 1978. https://doi.org/10.2514/3.55767b
std::array<double, 4> Quaternion(const std::array<double, 9>& r) {
    const double trace = r[0] + r[4] + r[8];
    std::array<double, 4> q{};
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(1.0 + trace);
        q = {(r[7] - r[5]) / s, (r[2] - r[6]) / s, (r[3] - r[1]) / s, 0.25 * s};
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const double s = 2.0 * std::sqrt(1.0 + r[0] - r[4] - r[8]);
        q = {0.25 * s, (r[1] + r[3]) / s, (r[2] + r[6]) / s, (r[7] - r[5]) / s};
    } else if (r[4] > r[8]) {
        const double s = 2.0 * std::sqrt(1.0 + r[4] - r[0] - r[8]);
        q = {(r[1] + r[3]) / s, 0.25 * s, (r[5] + r[7]) / s, (r[2] - r[6]) / s};
    } else {
        const double s = 2.0 * std::sqrt(1.0 + r[8] - r[0] - r[4]);
        q = {(r[2] + r[6]) / s, (r[5] + r[7]) / s, 0.25 * s, (r[3] - r[1]) / s};
    }
    return q;
}

void CheckRotation(const std::array<double, 9>& r) {
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                dot += r[3 * i + k] * r[3 * j + k];
            }
            if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > 1e-9) {
                throw std::invalid_argument("A node's pose must be a rotation");
            }
        }
    }
    const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                       r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0.0) {
        throw std::invalid_argument("A node's pose must be a rotation, not a reflection");
    }
}

}  // namespace

void WriteGlbScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                   const std::vector<SceneNode>& nodes) {
    if (meshes.empty() || nodes.empty()) {
        throw std::invalid_argument("A glTF scene needs meshes and nodes");
    }
    std::string binary;
    std::string views;
    std::string accessors;
    std::string primitives;
    std::size_t accessor = 0;
    const auto view = [&](std::size_t offset, std::size_t length, int target) {
        views += std::string(views.empty() ? "" : ",") + R"({"buffer":0,"byteOffset":)" +
                 std::to_string(offset) + R"(,"byteLength":)" + std::to_string(length) +
                 R"(,"target":)" + std::to_string(target) + "}";
    };
    std::size_t viewCount = 0;
    for (const SceneMesh& scene : meshes) {
        const TriangleMesh& mesh = scene.mesh;
        CheckMesh(mesh, scene.normals);
        if (mesh.positions.empty() || mesh.triangles.empty()) {
            throw std::invalid_argument("Cannot write an empty mesh as glTF");
        }
        Vec3d low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::infinity()};
        Vec3d high = low * -1.0;
        const std::size_t positionStart = binary.size();
        for (const Vec3d& position : mesh.positions) {
            const Vec3d p = YUp(position);
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
        view(positionStart, binary.size() - positionStart, 34962);
        const std::string vertices = std::to_string(mesh.positions.size());
        accessors += std::string(accessors.empty() ? "" : ",") + R"({"bufferView":)" +
                     std::to_string(viewCount++) + R"(,"componentType":5126,"count":)" + vertices +
                     R"(,"type":"VEC3","min":[)" + Number(low.x) + "," + Number(low.y) + "," +
                     Number(low.z) + R"(],"max":[)" + Number(high.x) + "," + Number(high.y) + "," +
                     Number(high.z) + "]}";
        std::string attributes = R"("POSITION":)" + std::to_string(accessor++);
        if (!scene.normals.empty()) {
            const std::size_t normalStart = binary.size();
            for (const Vec3d& normal : scene.normals) {
                const Vec3d n = YUp(normal);
                for (const double c : {n.x, n.y, n.z}) {
                    Put(binary, static_cast<float>(c));
                }
            }
            view(normalStart, binary.size() - normalStart, 34962);
            accessors += R"(,{"bufferView":)" + std::to_string(viewCount++) +
                         R"(,"componentType":5126,"count":)" + vertices + R"(,"type":"VEC3"})";
            attributes += R"(,"NORMAL":)" + std::to_string(accessor++);
        }
        const std::size_t indexStart = binary.size();
        for (const auto& t : mesh.triangles) {
            for (const std::uint32_t v : t) {
                Put(binary, v);
            }
        }
        view(indexStart, binary.size() - indexStart, 34963);
        accessors += R"(,{"bufferView":)" + std::to_string(viewCount++) +
                     R"(,"componentType":5125,"count":)" +
                     std::to_string(3 * mesh.triangles.size()) + R"(,"type":"SCALAR"})";
        primitives += std::string(primitives.empty() ? "" : ",") +
                      R"({"primitives":[{"attributes":{)" + attributes + R"(},"indices":)" +
                      std::to_string(accessor++) + R"(,"mode":4}]})";
    }
    std::string nodeList;
    std::string roots;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const SceneNode& node = nodes[n];
        if (node.mesh >= meshes.size()) {
            throw std::invalid_argument("A glTF node names a mesh the scene does not have");
        }
        for (const double s : {node.scale.x, node.scale.y, node.scale.z}) {
            if (!(std::isfinite(s) && s != 0.0)) {
                throw std::invalid_argument("Node scales must be finite and not zero");
            }
        }
        CheckRotation(node.pose.rotation);
        const auto& r = node.pose.rotation;
        const std::array<double, 9> yUp{r[0], r[2], -r[1], r[6], r[8], -r[7], -r[3], -r[5], r[4]};
        const auto q = Quaternion(yUp);
        const Vec3d t = YUp(node.pose.translation);
        nodeList += std::string(n == 0 ? "" : ",") + R"({"mesh":)" + std::to_string(node.mesh) +
                    R"(,"translation":[)" + Number(t.x) + "," + Number(t.y) + "," + Number(t.z) +
                    R"(],"rotation":[)" + Number(q[0]) + "," + Number(q[1]) + "," + Number(q[2]) +
                    "," + Number(q[3]) + R"(],"scale":[)" + Number(node.scale.x) + "," +
                    Number(node.scale.z) + "," + Number(node.scale.y) + "]}";
        roots += std::string(n == 0 ? "" : ",") + std::to_string(n);
    }
    std::string json = R"({"asset":{"version":"2.0","generator":"TerrainGen"},"scene":0,)"
                       R"("scenes":[{"nodes":[)" +
                       roots + R"(]}],"nodes":[)" + nodeList + R"(],"meshes":[)" + primitives +
                       R"(],"buffers":[{"byteLength":)" + std::to_string(binary.size()) +
                       R"(}],"bufferViews":[)" + views + R"(],"accessors":[)" + accessors + "]}";
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

void WriteGlb(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals) {
    WriteGlbScene(path, {{mesh, normals}}, {SceneNode{}});
}

}  // namespace terraingen
