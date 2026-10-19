#include "terraingen/mesh/io/mesh_io.h"

#include "terraingen/mesh/io/formats.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/util/text.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// The extension of `path`, lower case.
std::string Extension(const std::filesystem::path& path) {
    return Lower(path.extension().string());
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
        RequireScaleFactors(node.scale, "Node scales");
        RequireRotation(node.pose.rotation);
    }
}

}  // namespace

namespace detail {

Vec3d Placed(const SceneNode& node, const Vec3d& p) {
    return node.pose.Apply({p.x * node.scale.x, p.y * node.scale.y, p.z * node.scale.z});
}

Vec3d PlacedNormal(const SceneNode& node, const Vec3d& n) {
    const Vec3d rotated =
        node.pose.Rotate({n.x / node.scale.x, n.y / node.scale.y, n.z / node.scale.z});
    return rotated * (1.0 / Length(rotated));
}

}  // namespace detail

bool IsMeshPath(const std::filesystem::path& path) {
    return HasExtension(path, {".obj", ".gltf", ".glb", ".stl", ".icq"});
}

void WriteScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                const std::vector<SceneNode>& nodes) {
    const std::string extension = Extension(path);
    const bool gltf = extension == ".gltf" || extension == ".glb";
    if (extension != ".obj" && extension != ".stl" && !gltf) {
        throw std::invalid_argument("Mesh files must end in .obj, .gltf, .glb or .stl: " +
                                    path.string());
    }
    CheckScene(meshes, nodes);
    if (extension == ".obj") {
        detail::WriteObj(path, meshes, nodes);
    } else if (extension == ".stl") {
        detail::WriteStl(path, meshes, nodes);
    } else {
        detail::WriteGltf(path, extension == ".glb", meshes, nodes);
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
    } else if (extension == ".stl") {
        read = detail::ReadStl(path);
    } else if (extension == ".icq") {
        read.mesh = IcqToMesh(ReadIcq(path));
    } else {
        throw std::invalid_argument("Meshes are read from .obj, .gltf, .glb, .stl or .icq: " +
                                    path.string());
    }
    if (read.mesh.triangles.empty()) {
        throw std::runtime_error("No triangles in " + path.string());
    }
    ValidateMesh(read.mesh);
    return read;
}

}  // namespace terraingen
