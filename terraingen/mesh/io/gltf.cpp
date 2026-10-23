// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/io/formats.h"

#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include <cgltf_write.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace terraingen::detail {

std::array<double, 4> RotationQuaternion(const std::array<double, 9>& r) {
    const double trace = r[0] + r[4] + r[8];
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(1.0 + trace);
        return {(r[7] - r[5]) / s, (r[2] - r[6]) / s, (r[3] - r[1]) / s, 0.25 * s};
    }
    if (r[0] > r[4] && r[0] > r[8]) {
        const double s = 2.0 * std::sqrt(1.0 + r[0] - r[4] - r[8]);
        return {0.25 * s, (r[1] + r[3]) / s, (r[2] + r[6]) / s, (r[7] - r[5]) / s};
    }
    if (r[4] > r[8]) {
        const double s = 2.0 * std::sqrt(1.0 + r[4] - r[0] - r[8]);
        return {(r[1] + r[3]) / s, 0.25 * s, (r[5] + r[7]) / s, (r[2] - r[6]) / s};
    }
    const double s = 2.0 * std::sqrt(1.0 + r[8] - r[0] - r[4]);
    return {(r[2] + r[6]) / s, (r[5] + r[7]) / s, 0.25 * s, (r[3] - r[1]) / s};
}

namespace {

/// The cgltf document of a scene: every array sized before any pointer into it is taken, and the
/// binary buffer every accessor reads.
class GltfDocument {
  public:
    GltfDocument(const std::vector<SceneMesh>& meshes, const std::vector<SceneNode>& nodes)
        : views_(ViewCount(meshes)), accessors_(views_.size()), attributes_(2 * meshes.size()),
          materials_(meshes.size()), primitives_(meshes.size()), meshes_(meshes.size()),
          nodes_(nodes.size()), roots_(nodes.size()) {
        std::size_t view = 0;
        for (std::size_t m = 0; m < meshes.size(); ++m) {
            AddMesh(m, meshes[m], view);
        }
        for (std::size_t n = 0; n < nodes.size(); ++n) {
            AddNode(n, nodes[n]);
        }
    }

    /// Writes the document as .glb (`binary`), or as .gltf with its buffer in a .bin beside it.
    void Write(const std::filesystem::path& path, bool binary) {
        cgltf_buffer buffer{};
        buffer.size = binary_.size();
        std::string bin;
        if (!binary) {
            std::filesystem::path binPath = path;
            binPath.replace_extension(".bin");
            WriteFile(binPath, binary_);
            bin = binPath.filename().string();
            buffer.uri = bin.data();
        }
        for (cgltf_buffer_view& view : views_) {
            view.buffer = &buffer;
        }
        cgltf_scene scene{};
        scene.nodes = roots_.data();
        scene.nodes_count = roots_.size();
        cgltf_data data{};
        std::string version = "2.0";
        std::string generator = "TerrainGen";
        data.asset.version = version.data();
        data.asset.generator = generator.data();
        data.buffers = &buffer;
        data.buffers_count = 1;
        data.buffer_views = views_.data();
        data.buffer_views_count = views_.size();
        data.accessors = accessors_.data();
        data.accessors_count = accessors_.size();
        data.materials = materials_.data();
        data.materials_count = materials_.size();
        data.meshes = meshes_.data();
        data.meshes_count = meshes_.size();
        data.nodes = nodes_.data();
        data.nodes_count = nodes_.size();
        data.scenes = &scene;
        data.scenes_count = 1;
        data.scene = &scene;
        if (binary) {
            data.bin = binary_.data();
            data.bin_size = binary_.size();
        }
        cgltf_options options{};
        options.type = binary ? cgltf_file_type_glb : cgltf_file_type_gltf;
        if (cgltf_write_file(&options, path.string().c_str(), &data) != cgltf_result_success) {
            throw std::runtime_error("Cannot write " + path.string());
        }
    }

  private:
    static std::size_t ViewCount(const std::vector<SceneMesh>& meshes) {
        std::size_t count = 0;
        for (const SceneMesh& scene : meshes) {
            count += scene.normals.empty() ? 2u : 3u;
        }
        return count;
    }

    char* Name(const std::string& name) {
        return names_.emplace_back(name).data();
    }

    /// Ends a view and its accessor at `view` over the bytes added since `start`.
    cgltf_accessor* AddAccessor(std::size_t& view, std::size_t start, cgltf_buffer_view_type type,
                                cgltf_component_type component, cgltf_type kind,
                                std::size_t count) {
        cgltf_buffer_view& v = views_[view];
        v.offset = start;
        v.size = binary_.size() - start;
        v.type = type;
        cgltf_accessor& a = accessors_[view];
        a.component_type = component;
        a.type = kind;
        a.count = count;
        a.buffer_view = &v;
        ++view;
        return &a;
    }

    cgltf_accessor* AddVectors(std::size_t& view, const std::vector<Vec3d>& vectors, bool bounds) {
        const std::size_t start = binary_.size();
        constexpr float kInf = std::numeric_limits<float>::infinity();
        std::array<float, 3> low{kInf, kInf, kInf};
        std::array<float, 3> high{-kInf, -kInf, -kInf};
        for (const Vec3d& vector : vectors) {
            const Vec3d p = YUp(vector);
            const std::array<float, 3> stored{static_cast<float>(p.x), static_cast<float>(p.y),
                                              static_cast<float>(p.z)};
            for (std::size_t c = 0; c < 3; ++c) {
                Put(binary_, stored[c]);
                low[c] = std::min(low[c], stored[c]);
                high[c] = std::max(high[c], stored[c]);
            }
        }
        cgltf_accessor* accessor =
            AddAccessor(view, start, cgltf_buffer_view_type_vertices, cgltf_component_type_r_32f,
                        cgltf_type_vec3, vectors.size());
        if (bounds) {
            accessor->has_min = accessor->has_max = 1;
            std::copy(low.begin(), low.end(), accessor->min);
            std::copy(high.begin(), high.end(), accessor->max);
        }
        return accessor;
    }

    void AddMesh(std::size_t m, const SceneMesh& scene, std::size_t& view) {
        cgltf_attribute* attributes = &attributes_[2 * m];
        attributes[0] = {Name("POSITION"), cgltf_attribute_type_position, 0,
                         AddVectors(view, scene.mesh.positions, true)};
        std::size_t count = 1;
        if (!scene.normals.empty()) {
            attributes[1] = {Name("NORMAL"), cgltf_attribute_type_normal, 0,
                             AddVectors(view, scene.normals, false)};
            count = 2;
        }
        const std::size_t start = binary_.size();
        for (const auto& t : scene.mesh.triangles) {
            for (const std::uint32_t v : t) {
                Put(binary_, v);
            }
        }
        cgltf_material& material = materials_[m];
        material.name = Name(scene.material.name);
        material.has_pbr_metallic_roughness = 1;
        auto& pbr = material.pbr_metallic_roughness;
        for (std::size_t c = 0; c < 3; ++c) {
            pbr.base_color_factor[c] = static_cast<float>(scene.material.color[c]);
        }
        pbr.base_color_factor[3] = 1.0f;
        pbr.metallic_factor = 0.0f;
        pbr.roughness_factor = 1.0f;
        cgltf_primitive& primitive = primitives_[m];
        primitive.type = cgltf_primitive_type_triangles;
        primitive.indices =
            AddAccessor(view, start, cgltf_buffer_view_type_indices, cgltf_component_type_r_32u,
                        cgltf_type_scalar, 3 * scene.mesh.triangles.size());
        primitive.material = &material;
        primitive.attributes = attributes;
        primitive.attributes_count = count;
        meshes_[m].primitives = &primitive;
        meshes_[m].primitives_count = 1;
    }

    void AddNode(std::size_t n, const SceneNode& node) {
        cgltf_node& out = nodes_[n];
        out.mesh = &meshes_[node.mesh];
        const auto q = RotationQuaternion(YUp(node.pose.rotation));
        const Vec3d t = YUp(node.pose.translation);
        out.has_translation = out.has_rotation = out.has_scale = 1;
        const std::array<double, 3> translation{t.x, t.y, t.z};
        const std::array<double, 3> scale{node.scale.x, node.scale.z, node.scale.y};
        for (std::size_t c = 0; c < 3; ++c) {
            out.translation[c] = static_cast<float>(translation[c]);
            out.scale[c] = static_cast<float>(scale[c]);
        }
        for (std::size_t c = 0; c < 4; ++c) {
            out.rotation[c] = static_cast<float>(q[c]);
        }
        roots_[n] = &out;
    }

    std::string binary_;
    std::deque<std::string> names_;
    std::vector<cgltf_buffer_view> views_;
    std::vector<cgltf_accessor> accessors_;
    std::vector<cgltf_attribute> attributes_;
    std::vector<cgltf_material> materials_;
    std::vector<cgltf_primitive> primitives_;
    std::vector<cgltf_mesh> meshes_;
    std::vector<cgltf_node> nodes_;
    std::vector<cgltf_node*> roots_;
};

/// The position accessor of `primitive`, or null.
const cgltf_accessor* PositionAccessor(const cgltf_primitive& primitive) {
    const cgltf_accessor* positions = nullptr;
    for (std::size_t a = 0; a < primitive.attributes_count; ++a) {
        if (primitive.attributes[a].type == cgltf_attribute_type_position) {
            positions = primitive.attributes[a].data;
        }
    }
    return positions;
}

/// Adds the vertices of `positions`, placed by the column-major `world` transform, and the
/// triangles of `primitive` to `read`.
void AppendPrimitive(const cgltf_primitive& primitive, const cgltf_accessor& positions,
                     const cgltf_float (&world)[16], SceneMesh& read) {
    const auto offset = static_cast<std::uint32_t>(read.mesh.positions.size());
    for (std::size_t v = 0; v < positions.count; ++v) {
        cgltf_float q[3] = {};
        cgltf_accessor_read_float(&positions, v, q, 3);
        const auto axis = [&](std::size_t c) {
            return static_cast<double>(world[c] * q[0] + world[4 + c] * q[1] + world[8 + c] * q[2] +
                                       world[12 + c]);
        };
        read.mesh.positions.push_back(ZUp({axis(0), axis(1), axis(2)}));
    }
    const std::size_t count = primitive.indices ? primitive.indices->count : positions.count;
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        std::array<std::uint32_t, 3> triangle{};
        for (std::size_t c = 0; c < 3; ++c) {
            const std::size_t index =
                primitive.indices ? cgltf_accessor_read_index(primitive.indices, i + c) : i + c;
            triangle[c] = offset + static_cast<std::uint32_t>(index);
        }
        read.mesh.triangles.push_back(triangle);
    }
}

/// The name and base color of a glTF material.
MeshMaterial ReadMaterial(const cgltf_material& material) {
    MeshMaterial read;
    read.name = material.name ? material.name : "default";
    for (std::size_t c = 0; c < 3; ++c) {
        read.color[c] = static_cast<double>(material.pbr_metallic_roughness.base_color_factor[c]);
    }
    return read;
}

/// Adds the triangles of `node` and its children, placed by their world transforms, to `read`.
void AppendNodeTriangles(const cgltf_node& node, SceneMesh& read) {
    if (node.mesh != nullptr) {
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        for (std::size_t p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& primitive = node.mesh->primitives[p];
            const cgltf_accessor* positions = PositionAccessor(primitive);
            if (primitive.type != cgltf_primitive_type_triangles || positions == nullptr) {
                continue;
            }
            AppendPrimitive(primitive, *positions, world, read);
            if (primitive.material != nullptr && read.material == MeshMaterial{}) {
                read.material = ReadMaterial(*primitive.material);
            }
        }
    }
    for (std::size_t c = 0; c < node.children_count; ++c) {
        AppendNodeTriangles(*node.children[c], read);
    }
}

}  // namespace

void WriteGltf(const std::filesystem::path& path, bool binary, const std::vector<SceneMesh>& meshes,
               const std::vector<SceneNode>& nodes) {
    GltfDocument(meshes, nodes).Write(path, binary);
}

SceneMesh ReadGltf(const std::filesystem::path& path) {
    const std::string file = path.string();
    cgltf_options options{};
    cgltf_data* parsed = nullptr;
    if (cgltf_parse_file(&options, file.c_str(), &parsed) != cgltf_result_success) {
        throw std::runtime_error("Cannot read glTF " + file);
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), file.c_str()) != cgltf_result_success ||
        cgltf_validate(data.get()) != cgltf_result_success) {
        throw std::runtime_error("Cannot load the buffers of glTF " + file);
    }
    SceneMesh read;
    if (data->scene != nullptr) {
        for (std::size_t n = 0; n < data->scene->nodes_count; ++n) {
            AppendNodeTriangles(*data->scene->nodes[n], read);
        }
    } else {
        for (std::size_t n = 0; n < data->nodes_count; ++n) {
            if (data->nodes[n].parent == nullptr) {
                AppendNodeTriangles(data->nodes[n], read);
            }
        }
    }
    return read;
}

}  // namespace terraingen::detail
