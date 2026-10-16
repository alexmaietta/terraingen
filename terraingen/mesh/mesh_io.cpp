#include "terraingen/mesh/mesh_io.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

static_assert(std::endian::native == std::endian::little,
              "The mesh writers assume a little-endian host");

/// Throws for normals that are neither empty nor one per vertex, or a triangle indexing past the
/// vertices.
void CheckMesh(const TriangleMesh& mesh, const std::vector<Vec3d>& normals) {
    if (!normals.empty() && normals.size() != mesh.positions.size()) {
        throw std::invalid_argument("Normals must be empty or one per vertex");
    }
    ValidateMesh(mesh);
}

/// Appends `value`'s bytes, little-endian on the hosts supported.
template <typename T> void Put(std::string& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void Save(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
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

/// glTF bufferView targets, accessor component types and primitive modes.
constexpr int kArrayBuffer = 34962;
constexpr int kElementArrayBuffer = 34963;
constexpr int kFloatComponent = 5126;
constexpr int kUnsignedIntComponent = 5125;
constexpr int kTriangles = 4;

/// GLB header magic ("glTF"), version and chunk types ("JSON", "BIN").
constexpr std::uint32_t kGlbMagic = 0x46546C67u;
constexpr std::uint32_t kGlbVersion = 2;
constexpr std::uint32_t kJsonChunk = 0x4E4F534Au;
constexpr std::uint32_t kBinChunk = 0x004E4942u;

/// `value` as JSON with 9 significant digits, negative zero written as 0.
std::string Number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", value == 0.0 ? 0.0 : value);
    return buffer.data();
}

/// `items` separated by commas.
std::string Join(const std::vector<std::string>& items) {
    std::string joined;
    for (const std::string& item : items) {
        joined += (joined.empty() ? "" : ",") + item;
    }
    return joined;
}

/// A JSON array of `values`.
std::string NumberArray(std::initializer_list<double> values) {
    std::vector<std::string> items;
    for (const double value : values) {
        items.push_back(Number(value));
    }
    return "[" + Join(items) + "]";
}

/// glTF is y-up: a z-up point (x, y, z) is written as (x, z, -y).
Vec3d YUp(const Vec3d& p) {
    return {p.x, p.z, -p.y};
}

/// The rotation matrix `r` (row-major, z-up) in glTF's y-up frame.
std::array<double, 9> YUp(const std::array<double, 9>& r) {
    return {r[0], r[2], -r[1], r[6], r[8], -r[7], -r[3], -r[5], r[4]};
}

/// Collects the buffer, views, accessors, meshes and nodes of a binary glTF file. Each view holds
/// one accessor, so a view's index is its accessor's.
class GlbBuilder {
  public:
    /// Adds `scene`'s vertices, normals and triangles and the mesh that draws them.
    void AddMesh(const SceneMesh& scene) {
        const TriangleMesh& mesh = scene.mesh;
        CheckMesh(mesh, scene.normals);
        if (mesh.positions.empty() || mesh.triangles.empty()) {
            throw std::invalid_argument("Cannot write an empty mesh as glTF");
        }
        std::string attributes = R"("POSITION":)" + std::to_string(AddPositions(mesh.positions));
        if (!scene.normals.empty()) {
            attributes += R"(,"NORMAL":)" + std::to_string(AddNormals(scene.normals));
        }
        const std::size_t indices = AddIndices(mesh.triangles);
        meshes_.push_back(R"({"primitives":[{"attributes":{)" + attributes + R"(},"indices":)" +
                          std::to_string(indices) + R"(,"mode":)" + std::to_string(kTriangles) +
                          "}]}");
    }

    /// Adds an instance of mesh `node.mesh`, one of the `meshCount` added.
    void AddNode(const SceneNode& node, std::size_t meshCount) {
        if (node.mesh >= meshCount) {
            throw std::invalid_argument("A glTF node names a mesh the scene does not have");
        }
        for (const double s : {node.scale.x, node.scale.y, node.scale.z}) {
            if (!(std::isfinite(s) && s != 0.0)) {
                throw std::invalid_argument("Node scales must be finite and not zero");
            }
        }
        RequireRotation(node.pose.rotation);
        const auto q = RotationQuaternion(YUp(node.pose.rotation));
        const Vec3d t = YUp(node.pose.translation);
        nodes_.push_back(R"({"mesh":)" + std::to_string(node.mesh) + R"(,"translation":)" +
                         NumberArray({t.x, t.y, t.z}) + R"(,"rotation":)" +
                         NumberArray({q[0], q[1], q[2], q[3]}) + R"(,"scale":)" +
                         NumberArray({node.scale.x, node.scale.z, node.scale.y}) + "}");
    }

    /// The JSON chunk's text, padded with spaces to a multiple of 4 bytes.
    std::string Json() const {
        std::vector<std::string> roots;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            roots.push_back(std::to_string(n));
        }
        std::string json = R"({"asset":{"version":"2.0","generator":"TerrainGen"},"scene":0,)"
                           R"("scenes":[{"nodes":[)" +
                           Join(roots) + R"(]}],"nodes":[)" + Join(nodes_) + R"(],"meshes":[)" +
                           Join(meshes_) + R"(],"buffers":[{"byteLength":)" +
                           std::to_string(binary_.size()) + R"(}],"bufferViews":[)" + Join(views_) +
                           R"(],"accessors":[)" + Join(accessors_) + "]}";
        json.append((4 - json.size() % 4) % 4, ' ');
        return json;
    }

    /// The whole file: header, JSON chunk and BIN chunk.
    std::string Bytes() const {
        const std::string json = Json();
        std::string binary = binary_;
        binary.append((4 - binary.size() % 4) % 4, '\0');
        std::string bytes;
        Put(bytes, kGlbMagic);
        Put(bytes, kGlbVersion);
        Put(bytes, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
        Put(bytes, static_cast<std::uint32_t>(json.size()));
        Put(bytes, kJsonChunk);
        bytes += json;
        Put(bytes, static_cast<std::uint32_t>(binary.size()));
        Put(bytes, kBinChunk);
        bytes += binary;
        return bytes;
    }

  private:
    /// Adds a view of the buffer from `start` to its end, and returns its index.
    std::size_t AddView(std::size_t start, int target) {
        views_.push_back(R"({"buffer":0,"byteOffset":)" + std::to_string(start) +
                         R"(,"byteLength":)" + std::to_string(binary_.size() - start) +
                         R"(,"target":)" + std::to_string(target) + "}");
        return views_.size() - 1;
    }

    /// Adds `accessor`, whose view is the last added, and returns its index.
    std::size_t AddAccessor(int componentType, std::size_t count, const std::string& rest) {
        accessors_.push_back(R"({"bufferView":)" + std::to_string(views_.size() - 1) +
                             R"(,"componentType":)" + std::to_string(componentType) +
                             R"(,"count":)" + std::to_string(count) + rest + "}");
        return accessors_.size() - 1;
    }

    std::size_t AddPositions(const std::vector<Vec3d>& positions) {
        constexpr double kInf = std::numeric_limits<double>::infinity();
        Vec3d low{kInf, kInf, kInf};
        Vec3d high = low * -1.0;
        const std::size_t start = binary_.size();
        for (const Vec3d& position : positions) {
            const Vec3d p = YUp(position);
            const std::array<float, 3> stored{static_cast<float>(p.x), static_cast<float>(p.y),
                                              static_cast<float>(p.z)};
            for (const float c : stored) {
                Put(binary_, c);
            }
            low = {std::fmin(low.x, stored[0]), std::fmin(low.y, stored[1]),
                   std::fmin(low.z, stored[2])};
            high = {std::fmax(high.x, stored[0]), std::fmax(high.y, stored[1]),
                    std::fmax(high.z, stored[2])};
        }
        AddView(start, kArrayBuffer);
        return AddAccessor(kFloatComponent, positions.size(),
                           R"(,"type":"VEC3","min":)" + NumberArray({low.x, low.y, low.z}) +
                               R"(,"max":)" + NumberArray({high.x, high.y, high.z}));
    }

    std::size_t AddNormals(const std::vector<Vec3d>& normals) {
        const std::size_t start = binary_.size();
        for (const Vec3d& normal : normals) {
            const Vec3d n = YUp(normal);
            for (const double c : {n.x, n.y, n.z}) {
                Put(binary_, static_cast<float>(c));
            }
        }
        AddView(start, kArrayBuffer);
        return AddAccessor(kFloatComponent, normals.size(), R"(,"type":"VEC3")");
    }

    std::size_t AddIndices(const std::vector<std::array<std::uint32_t, 3>>& triangles) {
        const std::size_t start = binary_.size();
        for (const auto& t : triangles) {
            for (const std::uint32_t v : t) {
                Put(binary_, v);
            }
        }
        AddView(start, kElementArrayBuffer);
        return AddAccessor(kUnsignedIntComponent, 3 * triangles.size(), R"(,"type":"SCALAR")");
    }

    std::string binary_;
    std::vector<std::string> views_;
    std::vector<std::string> accessors_;
    std::vector<std::string> meshes_;
    std::vector<std::string> nodes_;
};

}  // namespace

void WriteGlbScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                   const std::vector<SceneNode>& nodes) {
    if (meshes.empty() || nodes.empty()) {
        throw std::invalid_argument("A glTF scene needs meshes and nodes");
    }
    GlbBuilder builder;
    for (const SceneMesh& mesh : meshes) {
        builder.AddMesh(mesh);
    }
    for (const SceneNode& node : nodes) {
        builder.AddNode(node, meshes.size());
    }
    Save(path, builder.Bytes());
}

void WriteGlb(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals) {
    WriteGlbScene(path, {{mesh, normals}}, {SceneNode{}});
}

}  // namespace terraingen
