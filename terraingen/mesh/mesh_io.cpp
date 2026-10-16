#include "terraingen/mesh/mesh_io.h"

#include "terraingen/mesh/formats.h"

#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

static_assert(std::endian::native == std::endian::little,
              "The mesh writers assume a little-endian host");

/// The extension of `path`, lower case.
std::string Extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

/// Throws for no meshes or nodes, an empty or invalid mesh, mismatched normals, a node naming no
/// mesh, or a node scale or pose that cannot be written.
void CheckScene(const std::vector<SceneMesh>& meshes, const std::vector<SceneNode>& nodes) {
    if (meshes.empty() || nodes.empty()) {
        throw std::invalid_argument("A mesh scene needs meshes and nodes");
    }
    for (const SceneMesh& scene : meshes) {
        if (scene.mesh.positions.empty() || scene.mesh.triangles.empty()) {
            throw std::invalid_argument("Cannot write an empty mesh");
        }
        if (!scene.normals.empty() && scene.normals.size() != scene.mesh.positions.size()) {
            throw std::invalid_argument("Normals must be empty or one per vertex");
        }
        ValidateMesh(scene.mesh);
    }
    for (const SceneNode& node : nodes) {
        if (node.mesh >= meshes.size()) {
            throw std::invalid_argument("A scene node names a mesh the scene does not have");
        }
        for (const double s : {node.scale.x, node.scale.y, node.scale.z}) {
            if (!(std::isfinite(s) && s != 0.0)) {
                throw std::invalid_argument("Node scales must be finite and not zero");
            }
        }
        RequireRotation(node.pose.rotation);
    }
}

/// Appends `value`'s bytes, little-endian on the hosts supported.
template <typename T> void Put(std::string& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

}  // namespace

namespace detail {

Vec3d Placed(const SceneNode& node, const Vec3d& p) {
    return node.pose.Apply({p.x * node.scale.x, p.y * node.scale.y, p.z * node.scale.z});
}

Vec3d PlacedNormal(const SceneNode& node, const Vec3d& n) {
    const Vec3d scaled{n.x / node.scale.x, n.y / node.scale.y, n.z / node.scale.z};
    const auto& r = node.pose.rotation;
    const Vec3d rotated{r[0] * scaled.x + r[1] * scaled.y + r[2] * scaled.z,
                        r[3] * scaled.x + r[4] * scaled.y + r[5] * scaled.z,
                        r[6] * scaled.x + r[7] * scaled.y + r[8] * scaled.z};
    return rotated * (1.0 / Length(rotated));
}

void Save(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

void WritePly(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
              const std::vector<SceneNode>& nodes) {
    bool normals = true;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    for (const SceneNode& node : nodes) {
        const SceneMesh& scene = meshes[node.mesh];
        normals = normals && !scene.normals.empty();
        vertices += scene.mesh.positions.size();
        triangles += scene.mesh.triangles.size();
    }
    std::string bytes =
        "ply\nformat binary_little_endian 1.0\ncomment TerrainGen\nelement vertex " +
        std::to_string(vertices) + "\nproperty float x\nproperty float y\nproperty float z\n";
    if (normals) {
        bytes += "property float nx\nproperty float ny\nproperty float nz\n";
    }
    bytes += "element face " + std::to_string(triangles) +
             "\nproperty list uchar uint vertex_indices\nend_header\n";
    for (const SceneNode& node : nodes) {
        const SceneMesh& scene = meshes[node.mesh];
        for (std::size_t v = 0; v < scene.mesh.positions.size(); ++v) {
            const Vec3d p = Placed(node, scene.mesh.positions[v]);
            for (const double c : {p.x, p.y, p.z}) {
                Put(bytes, static_cast<float>(c));
            }
            if (normals) {
                const Vec3d n = PlacedNormal(node, scene.normals[v]);
                for (const double c : {n.x, n.y, n.z}) {
                    Put(bytes, static_cast<float>(c));
                }
            }
        }
    }
    std::uint32_t offset = 0;
    for (const SceneNode& node : nodes) {
        const TriangleMesh& mesh = meshes[node.mesh].mesh;
        for (const auto& t : mesh.triangles) {
            Put(bytes, static_cast<std::uint8_t>(3));
            for (const std::uint32_t v : t) {
                Put(bytes, offset + v);
            }
        }
        offset += static_cast<std::uint32_t>(mesh.positions.size());
    }
    Save(path, bytes);
}

}  // namespace detail

bool IsMeshPath(const std::filesystem::path& path) {
    const std::string extension = Extension(path);
    return extension == ".ply" || extension == ".obj" || extension == ".gltf" ||
           extension == ".glb";
}

void WriteScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                const std::vector<SceneNode>& nodes) {
    if (!IsMeshPath(path)) {
        throw std::invalid_argument("Mesh files must end in .ply, .obj, .gltf or .glb: " +
                                    path.string());
    }
    CheckScene(meshes, nodes);
    const std::string extension = Extension(path);
    if (extension == ".ply") {
        detail::WritePly(path, meshes, nodes);
    } else if (extension == ".obj") {
        detail::WriteObj(path, meshes, nodes);
    } else {
        detail::WriteGltf(path, meshes, nodes);
    }
}

void WriteMesh(const std::filesystem::path& path, const SceneMesh& mesh) {
    WriteScene(path, {mesh}, {SceneNode{}});
}

SceneMesh ReadMesh(const std::filesystem::path& path) {
    const std::string extension = Extension(path);
    SceneMesh read;
    if (extension == ".obj") {
        read = detail::ReadObj(path);
    } else if (extension == ".gltf" || extension == ".glb") {
        read = detail::ReadGltf(path);
    } else {
        throw std::invalid_argument("Meshes are read from .obj, .gltf or .glb: " + path.string());
    }
    if (read.mesh.triangles.empty()) {
        throw std::runtime_error("No triangles in " + path.string());
    }
    ValidateMesh(read.mesh);
    return read;
}

}  // namespace terraingen
