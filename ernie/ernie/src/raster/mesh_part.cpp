#include "ernie/raster/mesh_part.h"

#include "embedded_shaders.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ernie {

namespace {

constexpr std::uint32_t kHasNormals = 1;
constexpr std::uint32_t kHasColors = 2;
constexpr std::uint32_t kInstanced = 4;
constexpr std::uint32_t kHasOrientations = 8;
constexpr std::uint32_t kHasScales = 16;

/// One draw as `mesh.slang` reads it.
struct GpuDraw {
    float model[16];
    float prototype[16];
    float tint[4];
    std::uint32_t offsets[4];
};
static_assert(sizeof(GpuDraw) == 160, "GpuDraw must match MeshDraw in mesh.slang");

struct DrawConstants {
    float lightViewProj[16];
    std::uint32_t first;
    std::uint32_t pad[3];
};
static_assert(sizeof(DrawConstants) == 80, "DrawConstants must match frame.slang");

constexpr VkShaderStageFlags kGraphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

/// Frame (0 to 4), material (5 to 9) and mesh (10 to 18) bindings; see mesh.slang.
constexpr DescriptorBinding kBindings[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {3, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
    {4, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
    {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {9, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
    {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
};

/// The mesh passes composed with reflectance model `model`.
std::vector<ShaderModuleSource> MeshModules(const BrdfModel& model) {
    return {{"brdf", std::string(BrdfSlang())},
            {"frame", shaders::kFrameSlang},
            {"surface", shaders::kSurfaceSlang},
            {BrdfModuleName(model), model.slang},
            {"mesh", "import " + BrdfModuleName(model) + ";\ntypealias ActiveBrdf = " +
                         model.typeName + ";\n" + shaders::kMeshSlang}};
}

/// A host-visible storage buffer holding `bytes`, at least 16 bytes so it can always be bound.
std::unique_ptr<GpuBuffer> StorageBuffer(const VulkanContext& context,
                                         std::span<const std::byte> bytes) {
    auto buffer =
        std::make_unique<GpuBuffer>(context, std::max<VkDeviceSize>(bytes.size(), 16),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload);
    if (!bytes.empty()) {
        buffer->Write(bytes);
    }
    return buffer;
}

template <typename T>
std::unique_ptr<GpuBuffer> StorageBuffer(const VulkanContext& context, const std::vector<T>& data) {
    return StorageBuffer(context, std::as_bytes(std::span(data)));
}

void CheckTriples(const std::vector<float>& values, std::size_t count, const char* what) {
    if (!values.empty() && values.size() != 3 * count) {
        throw std::invalid_argument(std::string(what) + " must have 3 values per point");
    }
}

void CheckInstances(const InstanceData& data) {
    if (data.positions.size() % 3 != 0) {
        throw std::invalid_argument("Instance positions must have 3 values per instance");
    }
    const std::size_t count = data.positions.size() / 3;
    if (!data.orientations.empty() && data.orientations.size() != 4 * count) {
        throw std::invalid_argument("Instance orientations must have 4 values per instance");
    }
    if (!data.scales.empty() && data.scales.size() != 3 * count) {
        throw std::invalid_argument("Instance scales must have 3 values per instance");
    }
}

std::pair<glm::vec3, glm::vec3> PointBounds(const std::vector<float>& points) {
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i + 2 < points.size(); i += 3) {
        const glm::vec3 p(points[i], points[i + 1], points[i + 2]);
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    return {low, high};
}

/// Bounds of the box [`low`, `high`] after `transform`.
std::pair<glm::vec3, glm::vec3> TransformBox(const glm::mat4& transform, const glm::vec3& low,
                                             const glm::vec3& high) {
    glm::vec3 outLow(std::numeric_limits<float>::max());
    glm::vec3 outHigh(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                               (i & 4) ? high.z : low.z);
        const glm::vec3 p(transform * glm::vec4(corner, 1.0f));
        outLow = glm::min(outLow, p);
        outHigh = glm::max(outHigh, p);
    }
    return {outLow, outHigh};
}

}  // namespace

std::vector<std::uint32_t> TriangulateFaces(std::span<const std::int64_t> faceVertexCounts,
                                            std::span<const std::int64_t> faceVertexIndices,
                                            std::size_t pointCount) {
    std::vector<std::uint32_t> triangles;
    std::size_t next = 0;
    for (const std::int64_t count : faceVertexCounts) {
        if (count < 3) {
            throw std::invalid_argument("Every face needs at least 3 vertices");
        }
        if (next + static_cast<std::size_t>(count) > faceVertexIndices.size()) {
            throw std::invalid_argument("faceVertexCounts add up to more than faceVertexIndices");
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(count); ++k) {
            const std::int64_t index = faceVertexIndices[next + k];
            if (index < 0 || static_cast<std::size_t>(index) >= pointCount) {
                throw std::invalid_argument("faceVertexIndices has an index out of range");
            }
        }
        for (std::size_t k = 1; k + 1 < static_cast<std::size_t>(count); ++k) {
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next]));
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next + k]));
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next + k + 1]));
        }
        next += static_cast<std::size_t>(count);
    }
    if (next != faceVertexIndices.size()) {
        throw std::invalid_argument("faceVertexCounts add up to fewer than faceVertexIndices");
    }
    return triangles;
}

MeshPart::MeshPart(RasterRenderer& renderer)
    : renderer_(renderer), context_(renderer.Context()),
      layout_(renderer.Context(), kBindings, sizeof(DrawConstants)) {
    const std::vector<std::uint32_t> vertex = renderer_.Compiler().CompileSpirv(
        MeshModules(renderer_.Brdfs().Models().front()), "meshShadowVertex");
    GraphicsPipelineDesc shadow;
    shadow.vertexSpirv = vertex;
    shadow.depthFormat = VK_FORMAT_D32_SFLOAT;
    shadow.depthBiasSlope = 1.5f;
    shadowPipeline_ = std::make_unique<GraphicsPipeline>(context_, layout_, shadow);
    RebuildGeometry();
    RebuildInstances();
    draws_ = StorageBuffer(context_, std::span<const std::byte>());
    instanceIds_ = StorageBuffer(context_, std::span<const std::byte>());
}

MeshPart::~MeshPart() {
    vkDeviceWaitIdle(context_.Device());
}

const GraphicsPipeline& MeshPart::ColorPipeline(const BrdfModel& model) {
    if (const auto it = colorPipelines_.find(model.id); it != colorPipelines_.end()) {
        return *it->second;
    }
    const std::vector<ShaderModuleSource> modules = MeshModules(model);
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
    try {
        vertex = renderer_.Compiler().CompileSpirv(modules, "meshVertexMain");
        fragment = renderer_.Compiler().CompileSpirv(modules, "meshFragmentMain");
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument("BRDF " + model.id +
                                    " failed to compile for meshes: " + error.what());
    }
    GraphicsPipelineDesc desc;
    desc.vertexSpirv = vertex;
    desc.fragmentSpirv = fragment;
    desc.colorFormat = renderer_.ColorFormat();
    desc.depthFormat = renderer_.DepthFormat();
    auto pipeline = std::make_unique<GraphicsPipeline>(context_, layout_, desc);
    return *colorPipelines_.emplace(model.id, std::move(pipeline)).first->second;
}

std::uint32_t MeshPart::AddMesh(MeshData data) {
    if (data.points.size() % 3 != 0 || data.indices.size() % 3 != 0) {
        throw std::invalid_argument("Mesh points and indices must come in threes");
    }
    const std::size_t count = data.points.size() / 3;
    CheckTriples(data.normals, count, "Mesh normals");
    CheckTriples(data.colors, count, "Mesh colours");
    for (const std::uint32_t index : data.indices) {
        if (index >= count) {
            throw std::invalid_argument("Mesh index out of range");
        }
    }
    Mesh mesh;
    std::tie(mesh.low, mesh.high) = PointBounds(data.points);
    mesh.data = std::move(data);
    const std::uint32_t handle = nextHandle_++;
    meshes_.emplace(handle, std::move(mesh));
    geometryDirty_ = true;
    return handle;
}

void MeshPart::SetPoints(std::uint32_t mesh, std::span<const float> points) {
    const auto it = meshes_.find(mesh);
    if (it == meshes_.end()) {
        throw std::invalid_argument("Unknown mesh " + std::to_string(mesh));
    }
    if (points.size() != it->second.data.points.size()) {
        throw std::invalid_argument("SetPoints must keep the number of points");
    }
    it->second.data.points.assign(points.begin(), points.end());
    std::tie(it->second.low, it->second.high) = PointBounds(it->second.data.points);
    it->second.pointsDirty = true;
}

void MeshPart::RemoveMesh(std::uint32_t mesh) {
    if (meshes_.erase(mesh) == 0) {
        throw std::invalid_argument("Unknown mesh " + std::to_string(mesh));
    }
    std::erase_if(items_, [mesh](const MeshItem& item) { return item.mesh == mesh; });
    itemPipelines_.clear();
    geometryDirty_ = true;
}

std::uint32_t MeshPart::AddInstancer(InstanceData data) {
    CheckInstances(data);
    const std::uint32_t handle = nextHandle_++;
    instancers_.emplace(handle, Instancer{std::move(data), 0});
    instancesDirty_ = true;
    return handle;
}

void MeshPart::SetInstances(std::uint32_t instancer, InstanceData data) {
    const auto it = instancers_.find(instancer);
    if (it == instancers_.end()) {
        throw std::invalid_argument("Unknown instancer " + std::to_string(instancer));
    }
    CheckInstances(data);
    it->second.data = std::move(data);
    instancesDirty_ = true;
}

void MeshPart::RemoveInstancer(std::uint32_t instancer) {
    if (instancers_.erase(instancer) == 0) {
        throw std::invalid_argument("Unknown instancer " + std::to_string(instancer));
    }
    std::erase_if(items_,
                  [instancer](const MeshItem& item) { return item.instancer == instancer; });
    itemPipelines_.clear();
    instancesDirty_ = true;
}

void MeshPart::SetItems(std::vector<MeshItem> items) {
    for (const MeshItem& item : items) {
        if (!meshes_.contains(item.mesh)) {
            throw std::invalid_argument("Unknown mesh " + std::to_string(item.mesh));
        }
        if (!renderer_.HasMaterial(item.material)) {
            throw std::invalid_argument("Unknown material " + std::to_string(item.material));
        }
        if (item.instancer) {
            const auto it = instancers_.find(*item.instancer);
            if (it == instancers_.end()) {
                throw std::invalid_argument("Unknown instancer " + std::to_string(*item.instancer));
            }
            const std::size_t count = it->second.data.positions.size() / 3;
            for (const std::uint32_t instance : item.instances) {
                if (instance >= count) {
                    throw std::invalid_argument("Instance index out of range");
                }
            }
        }
    }
    items_ = std::move(items);
    itemPipelines_.clear();
}

void MeshPart::RebuildGeometry() {
    std::vector<float> points;
    std::vector<float> normals;
    std::vector<float> colors;
    std::vector<std::uint32_t> indices;
    for (auto& [handle, mesh] : meshes_) {
        mesh.firstIndex = static_cast<std::uint32_t>(indices.size());
        mesh.firstVertex = static_cast<std::uint32_t>(points.size() / 3);
        const std::size_t count = mesh.data.points.size();
        points.insert(points.end(), mesh.data.points.begin(), mesh.data.points.end());
        if (mesh.data.normals.empty()) {
            normals.resize(normals.size() + count, 0.0f);
        } else {
            normals.insert(normals.end(), mesh.data.normals.begin(), mesh.data.normals.end());
        }
        if (mesh.data.colors.empty()) {
            colors.resize(colors.size() + count, 0.0f);
        } else {
            colors.insert(colors.end(), mesh.data.colors.begin(), mesh.data.colors.end());
        }
        indices.insert(indices.end(), mesh.data.indices.begin(), mesh.data.indices.end());
        mesh.pointsDirty = false;
    }
    vkDeviceWaitIdle(context_.Device());
    points_ = StorageBuffer(context_, points);
    normals_ = StorageBuffer(context_, normals);
    colors_ = StorageBuffer(context_, colors);
    indices_ = StorageBuffer(context_, indices);
    geometryDirty_ = false;
}

void MeshPart::RebuildInstances() {
    std::vector<float> positions;
    std::vector<float> orientations;
    std::vector<float> scales;
    for (auto& [handle, instancer] : instancers_) {
        instancer.first = static_cast<std::uint32_t>(positions.size() / 3);
        const std::size_t count = instancer.data.positions.size() / 3;
        positions.insert(positions.end(), instancer.data.positions.begin(),
                         instancer.data.positions.end());
        if (instancer.data.orientations.empty()) {
            orientations.resize(orientations.size() + 4 * count, 0.0f);
        } else {
            orientations.insert(orientations.end(), instancer.data.orientations.begin(),
                                instancer.data.orientations.end());
        }
        if (instancer.data.scales.empty()) {
            scales.resize(scales.size() + 3 * count, 1.0f);
        } else {
            scales.insert(scales.end(), instancer.data.scales.begin(), instancer.data.scales.end());
        }
    }
    const auto size = [](const std::unique_ptr<GpuBuffer>& buffer) {
        return buffer != nullptr ? buffer->Size() : VkDeviceSize{0};
    };
    if (size(positions_) == std::max<VkDeviceSize>(positions.size() * sizeof(float), 16)) {
        positions_->Write(std::as_bytes(std::span(positions)));
        orientations_->Write(std::as_bytes(std::span(orientations)));
        scales_->Write(std::as_bytes(std::span(scales)));
    } else {
        vkDeviceWaitIdle(context_.Device());
        positions_ = StorageBuffer(context_, positions);
        orientations_ = StorageBuffer(context_, orientations);
        scales_ = StorageBuffer(context_, scales);
    }
    instancesDirty_ = false;
}

void MeshPart::Prepare(const FrameParams&, std::span<const ShadowCascade>) {
    if (geometryDirty_) {
        RebuildGeometry();
    } else {
        for (auto& [handle, mesh] : meshes_) {
            if (mesh.pointsDirty) {
                points_->Write(std::as_bytes(std::span(mesh.data.points)),
                               VkDeviceSize{mesh.firstVertex} * 3 * sizeof(float));
                mesh.pointsDirty = false;
            }
        }
    }
    if (instancesDirty_) {
        RebuildInstances();
    }

    std::vector<GpuDraw> draws(items_.size());
    std::vector<std::uint32_t> instanceIds;
    itemPipelines_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MeshItem& item = items_[i];
        const Mesh& mesh = meshes_.at(item.mesh);
        const RasterMaterial& material = renderer_.MaterialOrDefault(item.material);
        itemPipelines_.push_back(&ColorPipeline(*renderer_.Brdfs().Find(material.Settings().brdf)));
        GpuDraw& draw = draws[i];
        std::memcpy(draw.model, glm::value_ptr(item.transform), sizeof(draw.model));
        std::memcpy(draw.prototype, glm::value_ptr(item.prototype), sizeof(draw.prototype));
        draw.tint[0] = item.tint.r;
        draw.tint[1] = item.tint.g;
        draw.tint[2] = item.tint.b;
        draw.offsets[0] = mesh.firstIndex;
        draw.offsets[1] = mesh.firstVertex;
        draw.offsets[2] = static_cast<std::uint32_t>(instanceIds.size());
        std::uint32_t flags = 0;
        flags |= mesh.data.normals.empty() ? 0u : kHasNormals;
        flags |= item.vertexColors && !mesh.data.colors.empty() ? kHasColors : 0u;
        if (item.instancer) {
            const Instancer& instancer = instancers_.at(*item.instancer);
            flags |= kInstanced;
            flags |= instancer.data.orientations.empty() ? 0u : kHasOrientations;
            flags |= instancer.data.scales.empty() ? 0u : kHasScales;
            for (const std::uint32_t instance : item.instances) {
                instanceIds.push_back(instancer.first + instance);
            }
        }
        draw.offsets[3] = flags;
    }
    const auto bytes = [](const auto& data) {
        return std::max<VkDeviceSize>(data.size() * sizeof(data[0]), 16);
    };
    if (draws_->Size() < bytes(draws)) {
        vkDeviceWaitIdle(context_.Device());
        draws_ = StorageBuffer(context_, draws);
    } else if (!draws.empty()) {
        draws_->Write(std::as_bytes(std::span(draws)));
    }
    if (instanceIds_->Size() < bytes(instanceIds)) {
        vkDeviceWaitIdle(context_.Device());
        instanceIds_ = StorageBuffer(context_, instanceIds);
    } else if (!instanceIds.empty()) {
        instanceIds_->Write(std::as_bytes(std::span(instanceIds)));
    }
}

std::optional<Bounds3> MeshPart::Bounds() const {
    std::optional<Bounds3> bounds;
    const auto add = [&bounds](const std::pair<glm::vec3, glm::vec3>& box) {
        bounds = bounds ? std::pair(glm::min(bounds->first, box.first),
                                    glm::max(bounds->second, box.second))
                        : box;
    };
    for (const MeshItem& item : items_) {
        const Mesh& mesh = meshes_.at(item.mesh);
        if (mesh.data.points.empty()) {
            continue;
        }
        if (!item.instancer) {
            add(TransformBox(item.transform, mesh.low, mesh.high));
            continue;
        }
        const auto [low, high] = TransformBox(item.prototype, mesh.low, mesh.high);
        const float radius = glm::length(glm::max(glm::abs(low), glm::abs(high)));
        const InstanceData& data = instancers_.at(*item.instancer).data;
        for (const std::uint32_t i : item.instances) {
            float scale = 1.0f;
            if (!data.scales.empty()) {
                scale = std::max({std::abs(data.scales[3 * i]), std::abs(data.scales[3 * i + 1]),
                                  std::abs(data.scales[3 * i + 2])});
            }
            const glm::vec3 p(data.positions[3 * i], data.positions[3 * i + 1],
                              data.positions[3 * i + 2]);
            add(TransformBox(item.transform, p - glm::vec3(radius * scale),
                             p + glm::vec3(radius * scale)));
        }
    }
    return bounds;
}

std::uint64_t MeshPart::TriangleCount() const {
    std::uint64_t triangles = 0;
    for (const MeshItem& item : items_) {
        const std::uint64_t perMesh = meshes_.at(item.mesh).data.indices.size() / 3;
        triangles += perMesh * (item.instancer ? item.instances.size() : 1u);
    }
    return triangles;
}

std::vector<DescriptorResource> MeshPart::Resources(const RasterFrame& frame,
                                                    std::uint32_t material) const {
    std::vector<DescriptorResource> resources = {frame.uniforms, frame.shadows, frame.shadowMap,
                                                 frame.shadowSampler, frame.clampSampler};
    renderer_.MaterialOrDefault(material).AppendResources(resources);
    for (const GpuBuffer* buffer :
         {points_.get(), normals_.get(), indices_.get(), draws_.get(), colors_.get(),
          positions_.get(), orientations_.get(), scales_.get(), instanceIds_.get()}) {
        resources.push_back(DescriptorResource::Of(*buffer));
    }
    return resources;
}

void MeshPart::Draw(VkCommandBuffer commands, std::size_t item, const glm::mat4& light) const {
    const MeshItem& draw = items_[item];
    const auto vertices = static_cast<std::uint32_t>(meshes_.at(draw.mesh).data.indices.size());
    const auto instances =
        static_cast<std::uint32_t>(draw.instancer ? draw.instances.size() : std::size_t{1});
    if (vertices == 0 || instances == 0) {
        return;
    }
    DrawConstants constants{};
    std::memcpy(constants.lightViewProj, glm::value_ptr(light), sizeof(constants.lightViewProj));
    constants.first = static_cast<std::uint32_t>(item);
    layout_.PushConstants(commands, &constants, sizeof(constants));
    vkCmdDraw(commands, vertices, instances, 0, 0);
}

void MeshPart::RecordShadow(VkCommandBuffer commands, const RasterFrame& frame, std::size_t,
                            const glm::mat4& lightViewProjection) const {
    if (itemPipelines_.empty()) {
        return;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_->Handle());
    layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                 Resources(frame, RasterRenderer::kDefaultMaterial));
    for (std::size_t i = 0; i < itemPipelines_.size(); ++i) {
        Draw(commands, i, lightViewProjection);
    }
}

void MeshPart::RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const {
    if (itemPipelines_.empty()) {
        return;
    }
    const GraphicsPipeline* bound = nullptr;
    std::optional<std::uint32_t> pushed;
    for (std::size_t i = 0; i < itemPipelines_.size(); ++i) {
        if (itemPipelines_[i] != bound) {
            bound = itemPipelines_[i];
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound->Handle());
            pushed.reset();
        }
        if (pushed != items_[i].material) {
            pushed = items_[i].material;
            layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Resources(frame, *pushed));
        }
        Draw(commands, i, glm::mat4(1.0f));
    }
}

}  // namespace ernie
