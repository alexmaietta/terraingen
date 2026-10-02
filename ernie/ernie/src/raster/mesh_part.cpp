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
    it->second.devicePoints = Tensor();
    it->second.bound = false;
    it->second.pointsDirty = true;
}

void MeshPart::SetPoints(std::uint32_t mesh, const Tensor& points,
                         std::optional<std::pair<glm::vec3, glm::vec3>> bounds) {
    const auto it = meshes_.find(mesh);
    if (it == meshes_.end()) {
        throw std::invalid_argument("Unknown mesh " + std::to_string(mesh));
    }
    CheckDeviceTensor(points, 3, "Mesh points");
    Mesh& target = it->second;
    if (static_cast<std::size_t>(points.Elements()) != target.data.points.size()) {
        throw std::invalid_argument("SetPoints must keep the number of points");
    }
    if (bounds) {
        std::tie(target.low, target.high) = *bounds;
    } else {
        const Tensor host = cuda_->Download(points);
        target.data.points.assign(host.As<float>(), host.As<float>() + host.Elements());
        std::tie(target.low, target.high) = PointBounds(target.data.points);
    }
    target.devicePoints = points;
    target.bound = false;
    target.pointsDirty = true;
}

void MeshPart::CopyDevicePoints(Mesh& mesh) {
    cuda_->CopyToBuffer(mesh.devicePoints, *points_,
                        VkDeviceSize{mesh.firstVertex} * 3 * sizeof(float));
}

std::shared_ptr<GpuBuffer> MeshPart::Pool(std::span<const std::byte> bytes) const {
    if (cuda_ == nullptr) {
        return StorageBuffer(context_, bytes);
    }
    auto buffer =
        std::make_shared<GpuBuffer>(context_, std::max<VkDeviceSize>(bytes.size(), 16),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Shared);
    if (!bytes.empty()) {
        buffer->Write(bytes);
    }
    return buffer;
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
    Instancer instancer;
    instancer.count = data.positions.size() / 3;
    instancer.data = std::move(data);
    instancers_.emplace(handle, std::move(instancer));
    instancesDirty_ = true;
    return handle;
}

void MeshPart::SetInstances(std::uint32_t instancer, InstanceData data) {
    const auto it = instancers_.find(instancer);
    if (it == instancers_.end()) {
        throw std::invalid_argument("Unknown instancer " + std::to_string(instancer));
    }
    CheckInstances(data);
    Instancer& target = it->second;
    const std::size_t count = data.positions.size() / 3;
    instancesDirty_ = instancesDirty_ || count != target.count;
    target.count = count;
    target.data = std::move(data);
    target.device = {};
    target.onDevice = false;
    target.dirty = true;
    target.bound = {};
    target.boundExtent.reset();
}

void MeshPart::CheckDeviceTensor(const Tensor& tensor, std::size_t components,
                                 const char* what) const {
    if (cuda_ == nullptr) {
        throw std::invalid_argument("CUDA inputs need MeshPart::SetCudaInterop");
    }
    const bool shaped =
        tensor.Rank() == 1 ||
        (tensor.Rank() == 2 && tensor.Shape()[1] == static_cast<std::int64_t>(components));
    if (tensor.Location() != Device{DeviceKind::Cuda, cuda_->Device()} ||
        tensor.Type() != DType::Float32 || !tensor.IsContiguous() || !shaped ||
        static_cast<std::size_t>(tensor.Elements()) % components != 0) {
        throw std::invalid_argument(std::string(what) + " must be a contiguous float32 tensor of " +
                                    std::to_string(components) + "-vectors on CUDA device " +
                                    std::to_string(cuda_->Device()));
    }
}

void MeshPart::SetInstances(std::uint32_t instancer, const DeviceInstanceData& data) {
    const auto it = instancers_.find(instancer);
    if (it == instancers_.end()) {
        throw std::invalid_argument("Unknown instancer " + std::to_string(instancer));
    }
    CheckDeviceTensor(data.positions, 3, "Instance positions");
    const auto count = static_cast<std::size_t>(data.positions.Elements()) / 3;
    if (!data.orientations.Empty()) {
        CheckDeviceTensor(data.orientations, 4, "Instance orientations");
        if (static_cast<std::size_t>(data.orientations.Elements()) != 4 * count) {
            throw std::invalid_argument("Instance orientations must have 4 values per instance");
        }
    }
    if (!data.scales.Empty()) {
        CheckDeviceTensor(data.scales, 3, "Instance scales");
        if (static_cast<std::size_t>(data.scales.Elements()) != 3 * count) {
            throw std::invalid_argument("Instance scales must have 3 values per instance");
        }
    }
    Instancer& target = it->second;
    instancesDirty_ = instancesDirty_ || count != target.count;
    target.count = count;
    target.device = data;
    target.onDevice = true;
    target.dirty = true;
    target.bound = {};
    target.boundExtent.reset();
    target.data = {};
    if (!data.extent) {
        const Tensor positions = cuda_->Download(data.positions);
        target.data.positions.assign(positions.As<float>(), positions.As<float>() + 3 * count);
        if (!data.scales.Empty()) {
            const Tensor scales = cuda_->Download(data.scales);
            target.data.scales.assign(scales.As<float>(), scales.As<float>() + 3 * count);
        }
    }
}

void MeshPart::SetCudaInterop(CudaInterop* cuda) {
    cuda_ = cuda;
    geometryDirty_ = true;
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
            const std::size_t count = it->second.count;
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
    std::map<std::uint32_t, std::uint32_t> oldFirst;
    for (const auto& [handle, mesh] : meshes_) {
        oldFirst[handle] = mesh.firstVertex;
    }
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
    const std::shared_ptr<GpuBuffer> oldPoints = points_;
    points_ = Pool(std::as_bytes(std::span(points)));
    normals_ = StorageBuffer(context_, normals);
    colors_ = StorageBuffer(context_, colors);
    indices_ = StorageBuffer(context_, indices);
    std::vector<VkBufferCopy> keep;
    for (auto& [handle, mesh] : meshes_) {
        if (!mesh.devicePoints.Empty() && cuda_ != nullptr) {
            CopyDevicePoints(mesh);
        }
        const auto old = oldFirst.find(handle);
        if (mesh.bound && oldPoints != nullptr && old != oldFirst.end() &&
            !mesh.data.points.empty()) {
            keep.push_back({VkDeviceSize{old->second} * 3 * sizeof(float),
                            VkDeviceSize{mesh.firstVertex} * 3 * sizeof(float),
                            mesh.data.points.size() * sizeof(float)});
        }
    }
    CopyRegions(oldPoints.get(), *points_, keep);
    geometryDirty_ = false;
}

void MeshPart::CopyRegions(const GpuBuffer* from, GpuBuffer& to,
                           const std::vector<VkBufferCopy>& regions) const {
    if (from == nullptr || regions.empty()) {
        return;
    }
    context_.Submit([&](VkCommandBuffer commands) {
        vkCmdCopyBuffer(commands, from->Handle(), to.Handle(),
                        static_cast<std::uint32_t>(regions.size()), regions.data());
    });
}

void MeshPart::RebuildInstances() {
    std::map<std::uint32_t, std::uint32_t> oldFirst;
    for (const auto& [handle, instancer] : instancers_) {
        oldFirst[handle] = instancer.first;
    }
    std::vector<float> positions;
    std::vector<float> orientations;
    std::vector<float> scales;
    for (auto& [handle, instancer] : instancers_) {
        instancer.first = static_cast<std::uint32_t>(positions.size() / 3);
        const std::size_t count = instancer.count;
        const InstanceData& data = instancer.data;
        const bool host = !instancer.onDevice;
        if (host && data.positions.size() == 3 * count) {
            positions.insert(positions.end(), data.positions.begin(), data.positions.end());
        } else {
            positions.resize(positions.size() + 3 * count, 0.0f);
        }
        if (host && data.orientations.size() == 4 * count) {
            orientations.insert(orientations.end(), data.orientations.begin(),
                                data.orientations.end());
        } else {
            orientations.resize(orientations.size() + 4 * count, 0.0f);
        }
        if (host && data.scales.size() == 3 * count) {
            scales.insert(scales.end(), data.scales.begin(), data.scales.end());
        } else {
            scales.resize(scales.size() + 3 * count, 1.0f);
        }
    }
    vkDeviceWaitIdle(context_.Device());
    const std::array<std::shared_ptr<GpuBuffer>, 3> old{positions_, orientations_, scales_};
    positions_ = Pool(std::as_bytes(std::span(positions)));
    orientations_ = Pool(std::as_bytes(std::span(orientations)));
    scales_ = Pool(std::as_bytes(std::span(scales)));
    std::array<std::vector<VkBufferCopy>, 3> keep;
    constexpr std::array<VkDeviceSize, 3> kComponents{3, 4, 3};
    for (auto& [handle, instancer] : instancers_) {
        if (instancer.onDevice) {
            WriteInstancer(instancer);
        }
        instancer.dirty = false;
        const auto from = oldFirst.find(handle);
        for (std::size_t a = 0; a < 3; ++a) {
            if (instancer.bound[a] && old[a] != nullptr && from != oldFirst.end() &&
                instancer.count > 0) {
                const VkDeviceSize stride = kComponents[a] * sizeof(float);
                keep[a].push_back(
                    {from->second * stride, instancer.first * stride, instancer.count * stride});
            }
        }
    }
    CopyRegions(old[0].get(), *positions_, keep[0]);
    CopyRegions(old[1].get(), *orientations_, keep[1]);
    CopyRegions(old[2].get(), *scales_, keep[2]);
    instancesDirty_ = false;
}

void MeshPart::RebuildPools() {
    if (geometryDirty_) {
        RebuildGeometry();
    }
    if (instancesDirty_) {
        RebuildInstances();
    }
}

void MeshPart::CheckBindable() const {
    if (cuda_ == nullptr) {
        throw std::invalid_argument("Binding pools for CUDA needs MeshPart::SetCudaInterop");
    }
    if (NeedsRebuild()) {
        throw std::invalid_argument("Rebuild the pools (RebuildPools) before binding");
    }
}

PoolRegion MeshPart::BindPoints(std::uint32_t mesh, std::optional<Bounds3> bounds) {
    const auto it = meshes_.find(mesh);
    if (it == meshes_.end()) {
        throw std::invalid_argument("Unknown mesh " + std::to_string(mesh));
    }
    CheckBindable();
    Mesh& target = it->second;
    if (target.pointsDirty) {
        if (!target.devicePoints.Empty()) {
            CopyDevicePoints(target);
        } else {
            points_->Write(std::as_bytes(std::span(target.data.points)),
                           VkDeviceSize{target.firstVertex} * 3 * sizeof(float));
        }
        target.pointsDirty = false;
    }
    target.bound = true;
    target.devicePoints = Tensor();
    if (bounds) {
        std::tie(target.low, target.high) = *bounds;
    }
    return {points_, VkDeviceSize{target.firstVertex} * 3 * sizeof(float),
            target.data.points.size() / 3, 3};
}

PoolRegion MeshPart::BindInstances(std::uint32_t instancer, InstanceArray array,
                                   std::optional<Bounds3> extent) {
    const auto it = instancers_.find(instancer);
    if (it == instancers_.end()) {
        throw std::invalid_argument("Unknown instancer " + std::to_string(instancer));
    }
    CheckBindable();
    Instancer& target = it->second;
    if (target.dirty) {
        WriteInstancer(target);
        target.dirty = false;
    }
    const auto index = static_cast<std::size_t>(array);
    target.bound[index] = true;
    if (extent) {
        target.boundExtent = extent;
    }
    const std::size_t components = array == InstanceArray::Orientations ? 4 : 3;
    const std::shared_ptr<GpuBuffer>& pool =
        array == InstanceArray::Positions
            ? positions_
            : (array == InstanceArray::Orientations ? orientations_ : scales_);
    return {pool, VkDeviceSize{target.first} * components * sizeof(float), target.count,
            components};
}

void MeshPart::WriteInstancer(const Instancer& instancer) {
    const VkDeviceSize first = instancer.first;
    if (instancer.onDevice) {
        cuda_->CopyToBuffer(instancer.device.positions, *positions_, first * 3 * sizeof(float));
        if (!instancer.device.orientations.Empty()) {
            cuda_->CopyToBuffer(instancer.device.orientations, *orientations_,
                                first * 4 * sizeof(float));
        }
        if (!instancer.device.scales.Empty()) {
            cuda_->CopyToBuffer(instancer.device.scales, *scales_, first * 3 * sizeof(float));
        }
        return;
    }
    const InstanceData& data = instancer.data;
    positions_->Write(std::as_bytes(std::span(data.positions)), first * 3 * sizeof(float));
    if (!data.orientations.empty()) {
        orientations_->Write(std::as_bytes(std::span(data.orientations)),
                             first * 4 * sizeof(float));
    }
    if (!data.scales.empty()) {
        scales_->Write(std::as_bytes(std::span(data.scales)), first * 3 * sizeof(float));
    }
}

void MeshPart::Prepare(const FrameParams&, std::span<const ShadowCascade>) {
    if (geometryDirty_) {
        RebuildGeometry();
    } else {
        for (auto& [handle, mesh] : meshes_) {
            if (!mesh.pointsDirty) {
                continue;
            }
            if (!mesh.devicePoints.Empty()) {
                CopyDevicePoints(mesh);
            } else {
                points_->Write(std::as_bytes(std::span(mesh.data.points)),
                               VkDeviceSize{mesh.firstVertex} * 3 * sizeof(float));
            }
            mesh.pointsDirty = false;
        }
    }
    if (instancesDirty_) {
        RebuildInstances();
    } else {
        for (auto& [handle, instancer] : instancers_) {
            if (instancer.dirty) {
                WriteInstancer(instancer);
                instancer.dirty = false;
            }
        }
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
            const bool orientations =
                instancer.bound[1] || (instancer.onDevice ? !instancer.device.orientations.Empty()
                                                          : !instancer.data.orientations.empty());
            const bool scales =
                instancer.bound[2] || (instancer.onDevice ? !instancer.device.scales.Empty()
                                                          : !instancer.data.scales.empty());
            flags |= orientations ? kHasOrientations : 0u;
            flags |= scales ? kHasScales : 0u;
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
        const Instancer& instancer = instancers_.at(*item.instancer);
        const std::optional<Bounds3>& extent =
            instancer.boundExtent ? instancer.boundExtent : instancer.device.extent;
        if ((instancer.onDevice || instancer.boundExtent) && extent) {
            add(TransformBox(item.transform, extent->first, extent->second));
            continue;
        }
        const auto [low, high] = TransformBox(item.prototype, mesh.low, mesh.high);
        const float radius = glm::length(glm::max(glm::abs(low), glm::abs(high)));
        const InstanceData& data = instancer.data;
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
