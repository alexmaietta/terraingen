// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/io/formats.h"
#include "terraingen/util/text.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <set>
#include <stdexcept>
#include <string>

namespace terraingen::detail {

namespace {

/// `value` with 9 significant digits, negative zero written as 0.
std::string Number(double value) {
    return FormatNumber(value == 0.0 ? 0.0 : value, 9);
}

/// An OBJ line of `tag` and a vector's components.
std::string VectorLine(const char* tag, const Vec3d& v) {
    return std::string(tag) + " " + Number(v.x) + " " + Number(v.y) + " " + Number(v.z) + "\n";
}

/// The MTL text of every distinct material of `meshes`, by name.
std::string Materials(const std::vector<SceneMesh>& meshes) {
    std::set<std::string> written;
    std::string mtl = "# TerrainGen\n";
    for (const SceneMesh& scene : meshes) {
        const MeshMaterial& material = scene.material;
        if (!written.insert(material.name).second) {
            continue;
        }
        const auto& c = material.color;
        mtl += "\nnewmtl " + material.name + "\nKa 0 0 0\nKd " + Number(c[0]) + " " + Number(c[1]) +
               " " + Number(c[2]) + "\nKs 0 0 0\nd 1\nillum 1\n";
    }
    return mtl;
}

/// The OBJ lines of one node: its object, material, vertices, normals and faces, numbered after
/// the `offset` vertices written before it.
std::string NodeObject(const SceneNode& node, std::size_t index, const SceneMesh& scene,
                       std::size_t offset) {
    std::string obj = "o node_" + std::to_string(index) + "\nusemtl " + scene.material.name + "\n";
    for (const Vec3d& p : scene.mesh.positions) {
        obj += VectorLine("v", YUp(Placed(node, p)));
    }
    for (const Vec3d& n : scene.normals) {
        obj += VectorLine("vn", YUp(PlacedNormal(node, n)));
    }
    for (const auto& t : scene.mesh.triangles) {
        obj += "f";
        for (const std::uint32_t v : t) {
            const std::string i = std::to_string(offset + v + 1);
            obj += " " + (scene.normals.empty() ? i : i + "//" + i);
        }
        obj += "\n";
    }
    return obj;
}

}  // namespace

void WriteObj(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
              const std::vector<SceneNode>& nodes) {
    std::filesystem::path mtl = path;
    mtl.replace_extension(".mtl");
    WriteFile(mtl, Materials(meshes));
    std::string obj = "# TerrainGen\nmtllib " + mtl.filename().string() + "\n";
    std::size_t offset = 0;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const SceneMesh& scene = meshes[nodes[n].mesh];
        obj += NodeObject(nodes[n], n, scene, offset);
        offset += scene.mesh.positions.size();
    }
    WriteFile(path, obj);
}

SceneMesh ReadObj(const std::filesystem::path& path) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;
    config.mtl_search_path = path.parent_path().string();
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string(), config)) {
        throw std::runtime_error("Cannot read " + path.string() + ": " + reader.Error());
    }
    SceneMesh read;
    const std::vector<tinyobj::real_t>& vertices = reader.GetAttrib().vertices;
    for (std::size_t v = 0; v + 2 < vertices.size(); v += 3) {
        read.mesh.positions.push_back(
            ZUp({static_cast<double>(vertices[v]), static_cast<double>(vertices[v + 1]),
                 static_cast<double>(vertices[v + 2])}));
    }
    for (const tinyobj::shape_t& shape : reader.GetShapes()) {
        const auto& indices = shape.mesh.indices;
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            read.mesh.triangles.push_back(
                {static_cast<std::uint32_t>(indices[i].vertex_index),
                 static_cast<std::uint32_t>(indices[i + 1].vertex_index),
                 static_cast<std::uint32_t>(indices[i + 2].vertex_index)});
        }
    }
    if (!reader.GetMaterials().empty()) {
        const tinyobj::material_t& material = reader.GetMaterials().front();
        read.material = {material.name,
                         {static_cast<double>(material.diffuse[0]),
                          static_cast<double>(material.diffuse[1]),
                          static_cast<double>(material.diffuse[2])}};
    }
    return read;
}

}  // namespace terraingen::detail
