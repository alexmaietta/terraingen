#include "ernie/raster/raster_delegate.h"

#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include "render/product_frame.h"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

float Number(const Scene& scene, const Path& path, std::string_view attribute, float fallback) {
    return static_cast<float>(GetNumber(scene, path, attribute, fallback));
}

std::invalid_argument UploadError(const Path& path, std::string_view attribute,
                                  const std::string& why) {
    return std::invalid_argument("Cannot upload " + std::string(attribute) + " of '" +
                                 path.String() + "': " + why);
}

/// `tensor` as a float32 raster with the grid in the prim's attributes.
RasterView RasterOf(const Scene& scene, const Path& path, const Tensor& tensor,
                    std::string_view origin, std::string_view cellSize, std::string_view noData) {
    if (tensor.Type() != DType::Float32 || tensor.Rank() != 2 ||
        tensor.Location().kind != DeviceKind::Cpu) {
        throw std::invalid_argument("expected a float32 (rows, cols) CPU tensor");
    }
    RasterView view;
    view.data = tensor.As<float>();
    view.height = static_cast<std::size_t>(tensor.Shape()[0]);
    view.width = static_cast<std::size_t>(tensor.Shape()[1]);
    const glm::dvec2 corner = GetVec2(scene, path, origin, {0.0, 0.0});
    view.originX = corner.x;
    view.originY = corner.y;
    view.cellSize = GetNumber(scene, path, cellSize, 1.0);
    if (scene.Get(path, noData) != nullptr) {
        view.noData = static_cast<float>(GetNumber(scene, path, noData, 0.0));
    }
    return view;
}

TerrainColorMode ColorModeOf(const std::string& token) {
    if (token == tokens::kHillshade) {
        return TerrainColorMode::Hillshade;
    }
    if (token == tokens::kColormap) {
        return TerrainColorMode::Colormap;
    }
    return TerrainColorMode::ShadedColormap;
}

/// A CPU array of `components`-vectors as floats, shaped (n, components) or flat.
std::vector<float> FloatsOf(const Tensor& tensor, std::size_t components, std::string_view what) {
    const bool shaped =
        tensor.Rank() == 2 && tensor.Shape()[1] == static_cast<std::int64_t>(components);
    if (tensor.Location().kind != DeviceKind::Cpu || !(tensor.Rank() == 1 || shaped) ||
        static_cast<std::size_t>(tensor.Elements()) % components != 0) {
        throw std::invalid_argument(std::string(what) + " must be a CPU array of " +
                                    std::to_string(components) + "-vectors");
    }
    const Tensor packed = tensor.Contiguous();
    const auto count = static_cast<std::size_t>(packed.Elements());
    if (packed.Type() == DType::Float32) {
        return {packed.As<float>(), packed.As<float>() + count};
    }
    if (packed.Type() == DType::Float64) {
        return {packed.As<double>(), packed.As<double>() + count};
    }
    throw std::invalid_argument(std::string(what) + " must be float32 or float64");
}

/// The prim's attribute as an array: a written tensor or, for small arrays, a float array value.
std::optional<std::vector<float>> FloatAttribute(const Scene& scene, const Path& path,
                                                 std::string_view attribute,
                                                 std::size_t components) {
    if (const Tensor* tensor = scene.Read(path, attribute)) {
        return FloatsOf(*tensor, components, attribute);
    }
    return std::nullopt;
}

/// Targets of a relationship holding one path or a list of them.
std::vector<Path> TargetsOf(const Scene& scene, const Path& path, std::string_view attribute) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return {};
    }
    if (const auto* list = value->Get<std::vector<Path>>()) {
        return *list;
    }
    if (const auto* single = value->Get<Path>()) {
        return {*single};
    }
    return {};
}

/// The constant colour of `primvars:displayColor`: a colour value or a one-row array.
std::optional<glm::vec3> ConstantDisplayColor(const Scene& scene, const Path& mesh) {
    if (scene.Get(mesh, tokens::kDisplayColor) != nullptr) {
        return glm::vec3(GetVec3(scene, mesh, tokens::kDisplayColor, glm::dvec3(0.5)));
    }
    if (const Tensor* tensor = scene.Read(mesh, tokens::kDisplayColor)) {
        if (tensor->Elements() == 3) {
            const std::vector<float> color = FloatsOf(*tensor, 3, tokens::kDisplayColor);
            return glm::vec3(color[0], color[1], color[2]);
        }
    }
    return std::nullopt;
}

std::string Describe(const Path& path, std::string_view what, const std::exception& error) {
    return "'" + path.String() + "' " + std::string(what) + ": " + error.what();
}

/// `shape` averaged over each of `bands` (eight samples per band) times `scale`.
template <typename Shape>
std::vector<float> BinShape(const Shape& shape, const SpectralBands& bands, double scale) {
    std::vector<float> values(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        double sum = 0.0;
        for (int k = 0; k < 8; ++k) {
            sum += shape(bands.Lower(i) + (k + 0.5) * bands.Width() / 8.0);
        }
        values[static_cast<std::size_t>(i)] = static_cast<float>(scale * sum / 8.0);
    }
    return values;
}

}  // namespace

GroundParams GroundParamsOf(const Scene& scene, const Path& ground) {
    using namespace tokens;
    GroundParams params;
    params.height = Number(scene, ground, kGroundHeight, params.height);
    params.checker = GetBool(scene, ground, kChecker, params.checker);
    params.checkerSize = Number(scene, ground, kCheckerSize, params.checkerSize);
    if (!(params.checkerSize > 0.0f) || !std::isfinite(params.checkerSize)) {
        throw std::invalid_argument("GroundPlane '" + ground.String() +
                                    "' needs a positive checker size");
    }
    params.checkerColors[0] =
        glm::vec3(GetVec3(scene, ground, kCheckerColor0, glm::dvec3(params.checkerColors[0])));
    params.checkerColors[1] =
        glm::vec3(GetVec3(scene, ground, kCheckerColor1, glm::dvec3(params.checkerColors[1])));
    return params;
}

TerrainParams TerrainParamsOf(const Scene& scene, const RenderPrims& prims) {
    using namespace tokens;
    TerrainParams params;
    params.exaggeration = Number(scene, prims.terrain, kExaggeration, 1.0f);
    const Path& settings = prims.settings;
    params.colorMode = ColorModeOf(GetToken(scene, settings, kColorMode, kShadedColormap));
    LodSettings& lod = params.lod;
    lod.enabled = GetBool(scene, settings, kLodEnable, lod.enabled);
    lod.detail = Number(scene, settings, kLodDetail, lod.detail);
    lod.morphStart = Number(scene, settings, kLodMorphStart, lod.morphStart);
    lod.freeze = GetBool(scene, settings, kLodFreeze, lod.freeze);
    lod.showLevels = GetBool(scene, settings, kLodShowLevels, lod.showLevels);
    return params;
}

RasterDelegate::RasterDelegate(const VulkanContext& context, VkFormat presentFormat,
                               BrdfRegistry brdfs)
    : context_(context), renderer_(context, presentFormat, std::move(brdfs)), terrain_(renderer_),
      meshes_(renderer_), ground_(renderer_) {}

RasterDelegate::~RasterDelegate() {
    context_.WaitForSubmissions();
}

const RendererInfo& RasterDelegate::Info() const {
    static const RendererInfo info{"raster", false};
    return info;
}

std::optional<Bounds3> RasterDelegate::SceneBounds() const {
    std::optional<Bounds3> bounds;
    for (const std::optional<Bounds3>& part : {terrain_.Bounds(), meshes_.Bounds()}) {
        if (!part) {
            continue;
        }
        bounds = bounds ? Bounds3{glm::min(bounds->first, part->first),
                                  glm::max(bounds->second, part->second)}
                        : *part;
    }
    return bounds;
}

const BrdfRegistry& RasterDelegate::Brdfs() const {
    return renderer_.Brdfs();
}

RenderStats RasterDelegate::Stats() const {
    RenderStats stats;
    stats.gpuBytes = renderer_.GpuBytes() + terrain_.GpuBytes() +
                     (viewTargets_ != nullptr ? viewTargets_->GpuBytes() : 0);
    for (const auto& [product, frames] : frames_) {
        for (const std::shared_ptr<ProductFrame>& frame : frames) {
            stats.gpuBytes += static_cast<const RenderTargets&>(*frame->images).GpuBytes();
        }
    }
    stats.triangles = terrain_.TriangleCount() + meshes_.TriangleCount();
    stats.counters.emplace_back(raster_stats::kTerrainPatches, terrain_.PatchCount());
    stats.counters.emplace_back(raster_stats::kTerrainLodLevels, terrain_.Lod().Levels());
    return stats;
}

RasterDelegate::Key
RasterDelegate::KeyOf(const Scene& scene, const Path& path,
                      std::initializer_list<std::string_view> attributes) const {
    Key key{path, {}};
    key.second[0] = sceneId_;
    std::size_t i = 1;
    for (const std::string_view attribute : attributes) {
        key.second.at(i++) = scene.AttributeVersion(path, attribute);
    }
    return key;
}

std::optional<std::uint32_t> RasterDelegate::MaterialId(const Path& path) const {
    const auto it = materials_.find(path);
    return it != materials_.end() ? std::optional(it->second.id) : std::nullopt;
}

std::uint32_t RasterDelegate::MaterialOf(const Scene& scene, const Path& prim) const {
    return MaterialId(BoundMaterial(scene, prim)).value_or(RasterRenderer::kDefaultMaterial);
}

void RasterDelegate::SyncMaterials(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    for (const Path& path : scene.Prims(kMaterial)) {
        seen.insert(path);
        auto [it, added] = materials_.try_emplace(path);
        SyncedMaterial& material = it->second;
        if (added) {
            material.id = renderer_.AddMaterial();
        }
        try {
            SyncMaterial(scene, path, material);
        } catch (const std::invalid_argument& error) {
            errors.emplace_back(error.what());
        }
    }
    for (auto it = materials_.begin(); it != materials_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        renderer_.RemoveMaterial(it->second.id);
        it = materials_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
}

void RasterDelegate::SyncMaterial(const Scene& scene, const Path& path, SyncedMaterial& material) {
    using namespace tokens;
    renderer_.SetMaterial(material.id, MaterialSettingsOf(scene, path, renderer_.Brdfs()));
    RasterMaterial& gpu = renderer_.Material(material.id);

    const Key albedoKey =
        KeyOf(scene, path, {kAlbedoMap, kAlbedoMapOrigin, kAlbedoMapCellSize, kAlbedoMapNoData});
    if (material.albedoMap != albedoKey) {
        material.albedoMap = albedoKey;
        try {
            if (const Tensor* tensor = scene.Read(path, kAlbedoMap)) {
                const Tensor albedo = tensor->Contiguous();
                gpu.SetAlbedoMap(RasterOf(scene, path, albedo, kAlbedoMapOrigin, kAlbedoMapCellSize,
                                          kAlbedoMapNoData));
            } else if (gpu.HasAlbedoMap()) {
                gpu.ClearAlbedoMap();
            }
        } catch (const std::invalid_argument& error) {
            throw UploadError(path, kAlbedoMap, error.what());
        }
    }
    for (const bool normal : {false, true}) {
        const std::string_view attribute = normal ? kDetailNormal : kDetailAlbedo;
        std::optional<Key>& cached = normal ? material.detailNormal : material.detailAlbedo;
        const Key key = KeyOf(scene, path, {attribute});
        if (cached == key) {
            continue;
        }
        cached = key;
        try {
            std::optional<Image> image;
            if (const Tensor* tensor = scene.Read(path, attribute)) {
                image =
                    ImageOfTensor(*tensor, normal ? ImageEncoding::Linear : ImageEncoding::Srgb);
            }
            if (normal) {
                gpu.SetDetailNormal(image ? &*image : nullptr);
            } else {
                gpu.SetDetailAlbedo(image ? &*image : nullptr);
            }
        } catch (const std::invalid_argument& error) {
            throw UploadError(path, attribute, error.what());
        }
    }
}

void RasterDelegate::SyncHeightfield(const Scene& scene, const Path& terrain) {
    using namespace tokens;
    if (terrain.IsEmpty() || scene.Read(terrain, kHeights) == nullptr) {
        if (heightfield_) {
            terrain_.ClearHeightfield();
            heightfield_.reset();
        }
        return;
    }
    const Key key = KeyOf(scene, terrain, {kHeights, kOrigin, kCellSize, kNoData});
    if (heightfield_ == key) {
        return;
    }
    heightfield_ = key;
    try {
        const Tensor heights = scene.Read(terrain, kHeights)->Contiguous();
        terrain_.SetHeightfield(RasterOf(scene, terrain, heights, kOrigin, kCellSize, kNoData));
    } catch (const std::invalid_argument& error) {
        throw UploadError(terrain, kHeights, error.what());
    }
}

CudaInterop* RasterDelegate::Cuda(std::string* why) {
    if (!cudaTried_) {
        cudaTried_ = true;
        cuda_ = CudaInterop::Create(context_, &cudaWhy_);
    }
    if (cuda_ == nullptr && why != nullptr) {
        *why = cudaWhy_;
    }
    return cuda_.get();
}

CudaInterop& RasterDelegate::RequireCuda() {
    std::string why;
    CudaInterop* cuda = Cuda(&why);
    if (cuda == nullptr) {
        throw std::invalid_argument("CUDA arrays need CUDA interop, which is unavailable: " + why);
    }
    if (!meshesOnCuda_) {
        meshes_.SetCudaInterop(cuda);
        meshesOnCuda_ = true;
    }
    return *cuda;
}

void RasterDelegate::SetMeshPoints(const Scene& scene, const Path& path, std::uint32_t handle) {
    const Tensor* tensor = scene.Read(path, tokens::kPoints);
    if (tensor != nullptr && tensor->Location().kind == DeviceKind::Cuda) {
        RequireCuda();
        meshes_.SetPoints(handle, *tensor, ExtentOf(scene, path));
        return;
    }
    const std::optional<std::vector<float>> points =
        FloatAttribute(scene, path, tokens::kPoints, 3);
    meshes_.SetPoints(handle, points.value_or(std::vector<float>()));
}

void RasterDelegate::SyncMeshes(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    std::vector<Path> paths;
    for (const std::string_view type :
         {kMesh, kCube, kSphere, kCylinder, kCone, kCapsule, kPlane}) {
        const std::vector<Path> ofType = scene.Prims(type);
        paths.insert(paths.end(), ofType.begin(), ofType.end());
    }
    for (const Path& path : paths) {
        seen.insert(path);
        const Key key = KeyOf(scene, path,
                              {kFaceVertexCounts, kFaceVertexIndices, kNormals, kDisplayColor,
                               kSize, kGeomRadius, kGeomHeight, kGeomWidth, kGeomLength, kAxis});
        const std::uint64_t points = scene.AttributeVersion(path, kPoints);
        const auto it = syncedMeshes_.find(path);
        if (it != syncedMeshes_.end() && it->second.key == key) {
            SyncedMesh& synced = it->second;
            if (synced.points == points) {
                continue;
            }
            synced.points = points;
            if (synced.handle != 0) {
                try {
                    SetMeshPoints(scene, path, synced.handle);
                    continue;
                } catch (const std::invalid_argument&) {
                }
            }
        }
        SyncedMesh synced;
        synced.key = key;
        synced.points = points;
        if (it != syncedMeshes_.end() && it->second.handle != 0) {
            meshes_.RemoveMesh(it->second.handle);
        }
        try {
            MeshData data = MeshDataOf(scene, path);
            synced.hasColors = !data.colors.empty();
            synced.tint = ConstantDisplayColor(scene, path).value_or(glm::vec3(0.5f));
            synced.handle = meshes_.AddMesh(std::move(data));
            const Tensor* tensor = scene.Read(path, kPoints);
            if (tensor != nullptr && tensor->Location().kind == DeviceKind::Cuda) {
                SetMeshPoints(scene, path, synced.handle);
            }
        } catch (const std::invalid_argument& error) {
            if (synced.handle != 0) {
                meshes_.RemoveMesh(synced.handle);
                synced.handle = 0;
            }
            errors.push_back(Describe(path, "mesh", error));
        }
        syncedMeshes_[path] = synced;
    }
    for (auto it = syncedMeshes_.begin(); it != syncedMeshes_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.handle != 0) {
            meshes_.RemoveMesh(it->second.handle);
        }
        it = syncedMeshes_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument("Cannot upload " + errors.front());
    }
}

void RasterDelegate::SyncInstancers(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    for (const Path& path : scene.Prims(kPointInstancer)) {
        seen.insert(path);
        const Key key = KeyOf(scene, path, {kPositions, kOrientations, kScales});
        auto [it, added] = instancers_.try_emplace(path);
        SyncedInstancer& synced = it->second;
        if (!added && synced.key == key) {
            continue;
        }
        synced.key = key;
        try {
            const Tensor* positions = scene.Read(path, kPositions);
            if (positions != nullptr && positions->Location().kind == DeviceKind::Cuda) {
                CudaInterop& cuda = RequireCuda();
                const auto onDevice = [&](std::string_view attribute) {
                    const Tensor* tensor = scene.Read(path, attribute);
                    if (tensor == nullptr) {
                        return Tensor();
                    }
                    return tensor->Location().kind == DeviceKind::Cuda ? *tensor
                                                                       : cuda.Upload(*tensor);
                };
                DeviceInstanceData data;
                data.positions = *positions;
                data.orientations = onDevice(kOrientations);
                data.scales = onDevice(kScales);
                data.extent = ExtentOf(scene, path);
                if (synced.handle == 0) {
                    synced.handle = meshes_.AddInstancer({});
                }
                meshes_.SetInstances(synced.handle, data);
            } else {
                InstanceData data = InstanceDataOf(scene, path);
                if (synced.handle == 0) {
                    synced.handle = meshes_.AddInstancer(std::move(data));
                } else {
                    meshes_.SetInstances(synced.handle, std::move(data));
                }
            }
        } catch (const std::invalid_argument& error) {
            if (synced.handle != 0) {
                meshes_.RemoveInstancer(synced.handle);
                synced.handle = 0;
            }
            errors.push_back(Describe(path, "instancer", error));
        }
    }
    for (auto it = instancers_.begin(); it != instancers_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.handle != 0) {
            meshes_.RemoveInstancer(it->second.handle);
        }
        it = instancers_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument("Cannot upload " + errors.front());
    }
}

void RasterDelegate::Sync(const Scene& scene) {
    if (scene.Id() != sceneId_) {
        sceneId_ = scene.Id();
        cursor_.version = scene.Version();
    } else if (scene.Changes(cursor_).empty()) {
        return;
    }
    std::vector<std::string> errors;
    const auto attempt = [&errors](auto&& step) {
        try {
            step();
        } catch (const std::invalid_argument& error) {
            errors.emplace_back(error.what());
        }
    };
    attempt([&] { SyncMaterials(scene); });
    attempt([&] { SyncHeightfield(scene, ResolveRenderPrims(scene, {}).terrain); });
    attempt([&] { SyncMeshes(scene); });
    attempt([&] { SyncInstancers(scene); });
    if (!errors.empty()) {
        std::string message = errors.front();
        for (std::size_t i = 1; i < errors.size(); ++i) {
            message += "; " + errors[i];
        }
        throw std::invalid_argument(message);
    }
}

std::vector<MeshItem> RasterDelegate::Items(const Scene& scene) const {
    std::set<Path> prototypes;
    for (const auto& [path, synced] : instancers_) {
        for (const Path& prototype : TargetsOf(scene, path, tokens::kPrototypes)) {
            prototypes.insert(prototype);
        }
    }
    const auto isPrototype = [&prototypes](const Path& path) {
        return std::any_of(prototypes.begin(), prototypes.end(),
                           [&path](const Path& prototype) { return path.HasPrefix(prototype); });
    };
    const auto shade = [this, &scene](const Path& mesh, const SyncedMesh& synced, MeshItem& item) {
        item.material = MaterialOf(scene, mesh);
        const bool bound = item.material != RasterRenderer::kDefaultMaterial;
        item.tint = bound ? glm::vec3(1.0f) : synced.tint;
        item.vertexColors = !bound;
    };

    std::vector<MeshItem> items;
    for (const auto& [path, synced] : syncedMeshes_) {
        if (synced.handle == 0 || isPrototype(path) || !scene.IsVisible(path)) {
            continue;
        }
        MeshItem item;
        item.mesh = synced.handle;
        item.transform = glm::mat4(scene.WorldTransform(path));
        item.primId = PrimIdOf(path);
        shade(path, synced, item);
        items.push_back(std::move(item));
    }
    for (const auto& [path, synced] : instancers_) {
        if (synced.handle == 0 || !scene.IsVisible(path)) {
            continue;
        }
        const std::vector<Path> targets = TargetsOf(scene, path, tokens::kPrototypes);
        const std::vector<std::vector<std::uint32_t>> lists =
            InstancesByPrototype(scene, path, targets.size());
        const glm::mat4 world(scene.WorldTransform(path));
        for (std::size_t k = 0; k < targets.size(); ++k) {
            const auto mesh = syncedMeshes_.find(targets[k]);
            if (mesh == syncedMeshes_.end() || mesh->second.handle == 0 || lists[k].empty()) {
                continue;
            }
            MeshItem item;
            item.mesh = mesh->second.handle;
            item.transform = world;
            item.prototype = glm::mat4(scene.LocalTransform(targets[k]));
            item.instancer = synced.handle;
            item.instances = lists[k];
            item.primId = PrimIdOf(path);
            shade(targets[k], mesh->second, item);
            items.push_back(std::move(item));
        }
    }
    return items;
}

void RasterDelegate::Update(const Scene& scene, const Path& settings, VkExtent2D extent) {
    const RenderPrims prims = ResolveRenderPrims(scene, settings);
    UpdatePrims(scene, prims, extent, NextFrame(scene, prims.settings, prims.settings));
}

void RasterDelegate::UpdatePrims(const Scene& scene, const RenderPrims& prims, VkExtent2D extent,
                                 FrameId id, std::span<const SensorFilter> filters) {
    context_.WaitForSubmissions();
    Sync(scene);
    prims_ = prims;
    FrameParams frame = FrameParamsOf(scene, prims_, extent.width, extent.height);
    frame.filters.assign(filters.begin(), filters.end());
    frame.frame = id.frame;
    frame.stream = id.stream;
    TerrainParams terrain = TerrainParamsOf(scene, prims_);
    terrain.material = MaterialOf(scene, prims_.terrain);
    terrain.primId = prims_.terrain.IsEmpty() ? 0u : PrimIdOf(prims_.terrain);
    terrain_.SetParams(terrain);
    meshes_.SetItems(Items(scene));
    ground_.SetEnabled(!prims_.ground.IsEmpty());
    if (!prims_.ground.IsEmpty()) {
        GroundParams ground = GroundParamsOf(scene, prims_.ground);
        ground.material = MaterialOf(scene, prims_.ground);
        ground.primId = PrimIdOf(prims_.ground);
        ground_.SetParams(ground);
    }
    RasterPart* parts[] = {&terrain_, &meshes_, &ground_};
    renderer_.Update(frame, parts);
}

std::uint32_t RasterDelegate::PrimIdOf(const Path& path) const {
    const auto [it, added] =
        primIds_.try_emplace(path, static_cast<std::uint32_t>(primPaths_.size() + 1));
    if (added) {
        primPaths_.push_back(path);
    }
    return it->second;
}

Path RasterDelegate::PrimPath(std::uint32_t id) const {
    return id >= 1 && id <= primPaths_.size() ? primPaths_[id - 1] : Path();
}

std::vector<TimelinePoint> RasterDelegate::SubmitWaits() const {
    if (cuda_ == nullptr || cuda_->VulkanWait().semaphore == VK_NULL_HANDLE) {
        return {};
    }
    return {cuda_->VulkanWait()};
}

void RasterDelegate::Record(VkCommandBuffer commands, const RenderingTarget& target) {
    const VkExtent2D extent = renderer_.Extent();
    if (viewTargets_ == nullptr || viewTargets_->Extent().width != extent.width ||
        viewTargets_->Extent().height != extent.height ||
        viewTargets_->Bands() != renderer_.Bands().count) {
        if (extent.width == 0 || extent.height == 0) {
            BeginRendering(commands, target);
            vkCmdEndRendering(commands);
            return;
        }
        vkDeviceWaitIdle(context_.Device());
        viewTargets_ =
            std::make_unique<RenderTargets>(context_, extent, renderer_.Bands().count, false);
    }
    const RasterPart* parts[] = {&terrain_, &meshes_, &ground_};
    renderer_.Record(commands, *viewTargets_, parts, &target);
}

AttributeBinding RasterDelegate::Bind(const Scene& scene, const Path& path,
                                      std::string_view attribute) {
    using namespace tokens;
    const bool mesh = scene.Has(path) && scene.TypeOf(path) == kMesh;
    const bool instancer = scene.Has(path) && scene.TypeOf(path) == kPointInstancer;
    if (!(mesh && attribute == kPoints) &&
        !(instancer &&
          (attribute == kPositions || attribute == kOrientations || attribute == kScales))) {
        throw std::invalid_argument("Only a Mesh's points and a PointInstancer's positions, "
                                    "orientations and scales can be bound, not " +
                                    std::string(attribute) + " of '" + path.String() + "'");
    }
    RequireCuda();
    Sync(scene);
    const bool synced = mesh ? syncedMeshes_.contains(path) && syncedMeshes_.at(path).handle != 0
                             : instancers_.contains(path) && instancers_.at(path).handle != 0;
    if (!synced) {
        throw std::invalid_argument("'" + path.String() + "' did not sync, so it cannot be bound");
    }
    return MakeBinding(path, std::string(attribute), ExtentOf(scene, path));
}

namespace {

InstanceArray InstanceArrayOf(const std::string& attribute) {
    return attribute == tokens::kPositions      ? InstanceArray::Positions
           : attribute == tokens::kOrientations ? InstanceArray::Orientations
                                                : InstanceArray::Scales;
}

}  // namespace

std::uint32_t RasterDelegate::BoundHandle(const AttributeBinding& binding) const {
    if (binding.Attribute() == tokens::kPoints) {
        const auto it = syncedMeshes_.find(binding.GetPath());
        if (it == syncedMeshes_.end() || it->second.handle == 0) {
            throw std::invalid_argument("Bound mesh '" + binding.GetPath().String() + "' is gone");
        }
        return it->second.handle;
    }
    const auto it = instancers_.find(binding.GetPath());
    if (it == instancers_.end() || it->second.handle == 0) {
        throw std::invalid_argument("Bound instancer '" + binding.GetPath().String() + "' is gone");
    }
    return it->second.handle;
}

Tensor RasterDelegate::MapBinding(const AttributeBinding& binding, std::uintptr_t stream) {
    CudaInterop& cuda = RequireCuda();
    const std::uint32_t handle = BoundHandle(binding);
    const BoundSlot slot =
        binding.Attribute() == tokens::kPoints
            ? meshes_.MapPoints(handle, binding.Extent())
            : meshes_.MapInstances(handle, InstanceArrayOf(binding.Attribute()), binding.Extent());
    cuda.WaitForVulkan(stream, slot.readDone);
    return cuda.View(
        slot.buffer, DType::Float32,
        {static_cast<std::int64_t>(slot.count), static_cast<std::int64_t>(slot.components)});
}

void RasterDelegate::UnmapBinding(const AttributeBinding& binding, std::uintptr_t stream) {
    const std::uint32_t handle = BoundHandle(binding);
    if (binding.Attribute() == tokens::kPoints) {
        meshes_.UnmapPoints(handle);
    } else {
        meshes_.UnmapInstances(handle, InstanceArrayOf(binding.Attribute()));
    }
    RequireCuda().SignalVulkan(stream);
}

std::shared_ptr<ProductFrame> RasterDelegate::AcquireFrame(const Path& product, VkExtent2D extent,
                                                           int bands) {
    std::vector<std::shared_ptr<ProductFrame>>& frames = frames_[product];
    const bool shared = cuda_ != nullptr;
    std::erase_if(frames, [&](const std::shared_ptr<ProductFrame>& frame) {
        const auto& targets = static_cast<const RenderTargets&>(*frame->images);
        return frame.use_count() == 1 && frame->arrayMaps == 0 &&
               (targets.Extent().width != extent.width ||
                targets.Extent().height != extent.height || targets.Bands() != bands ||
                targets.Shared() != shared);
    });
    for (const std::shared_ptr<ProductFrame>& frame : frames) {
        if (frame.use_count() == 1 && frame->arrayMaps == 0) {
            return frame;
        }
    }
    auto frame = std::make_shared<ProductFrame>();
    frame->context = &context_;
    frame->cuda = cuda_.get();
    frame->linearPool = &linearPool_;
    frame->images = std::make_unique<RenderTargets>(context_, extent, bands, shared);
    frames.push_back(frame);
    return frame;
}

StepOperation RasterDelegate::StepAsync(const Scene& scene, std::span<const Path> products,
                                        const Path& settings) {
    context_.WaitForSubmissions();
    CudaInterop* cuda = Cuda();
    const RenderPrims base = ResolveRenderPrims(scene, settings);
    const std::vector<RenderProductSpec> specs = ResolveRenderProducts(scene, products, settings);
    RenderProductSetOutputs outputs;
    std::uint64_t submission = 0;
    for (const RenderProductSpec& spec : specs) {
        RenderPrims prims = base;
        if (!spec.camera.IsEmpty()) {
            prims.camera = spec.camera;
        }
        if (prims.camera.IsEmpty()) {
            throw std::invalid_argument("The scene has no Camera");
        }
        const int bands =
            CameraOf(scene, prims.camera, spec.resolution.width, spec.resolution.height)
                .bands.count;
        std::shared_ptr<ProductFrame> frame = AcquireFrame(spec.product, spec.resolution, bands);
        UpdatePrims(scene, prims, spec.resolution, NextFrame(scene, base.settings, spec.product),
                    spec.filters);
        const std::vector<TimelinePoint> waits = SubmitWaits();
        std::vector<TimelinePoint> signals;
        if (cuda != nullptr) {
            signals.push_back(cuda->NextVulkanSignal());
        }
        auto& targets = static_cast<RenderTargets&>(*frame->images);
        const RasterPart* parts[] = {&terrain_, &meshes_, &ground_};
        submission = context_.SubmitAsync(
            [&](VkCommandBuffer commands) { renderer_.Record(commands, targets, parts); }, waits,
            signals);
        if (!signals.empty()) {
            meshes_.MarkRead(signals.front().value);
        }
        frame->submission = submission;
        RenderProductOutput& output = outputs[spec.product];
        output.product = spec.product;
        for (const auto& [path, name] : spec.vars) {
            output.vars.emplace(name, RenderVar(frame, path, name));
        }
    }
    return StepOperation(context_, submission, std::move(outputs));
}

}  // namespace ernie
