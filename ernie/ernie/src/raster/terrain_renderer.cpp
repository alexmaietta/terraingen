#include "ernie/raster/terrain_renderer.h"

#include "core/raster_checks.h"
#include "embedded_shaders.h"
#include "ernie/raster/poisson_disk.h"

#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ernie {

namespace {

constexpr std::size_t kInitialPatchCapacity = 256;

struct Uniforms {
    float viewProjection[16];
    float sun[4];
    float grid[4];
    float heightRange[4];
    std::uint32_t dims[4];
    float forward[4];
    float lodEye[4];
    std::uint32_t lodInfo[4];
    float morph[LodQuadtree::kMaxLevels * 2];
    float eye[4];
};
static_assert(sizeof(Uniforms) == 320, "Uniforms must match terrain.slang");
static_assert(sizeof(LodPatch) == 16, "LodPatch must match the shaders' uint4");

struct ShadowUniforms {
    float lightViewProjection[4][16];
    float splits[4];
    float texelWorld[4];
    float params[4];
    std::uint32_t debug[4];
    float poisson[32];
};
static_assert(sizeof(ShadowUniforms) == 448, "ShadowUniforms must match terrain_color.slang");

// Per-draw push constants (DrawConstants in terrain.slang).
struct DrawConstants {
    float lightViewProj[16];
    std::uint32_t patchOffset;
    std::uint32_t pad[3];
};
static_assert(sizeof(DrawConstants) == 80, "DrawConstants must match terrain.slang");

constexpr VkShaderStageFlags kGraphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The push-descriptor set every terrain pass binds (see terrain.slang).
constexpr DescriptorBinding kBindings[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {4, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
    {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {7, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
    {8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {12, VK_DESCRIPTOR_TYPE_SAMPLER, kGraphics},
};

std::vector<ShaderModuleSource> TerrainModules() {
    return {{"brdf", std::string(BrdfSlang())},
            {"terrain", shaders::kTerrainSlang},
            {"surface", shaders::kSurfaceSlang}};
}

// Central-difference gradients (dz/dx east, dz/dy north) per cell as packed half floats, clamping
// at the raster's edges.
std::vector<std::uint32_t> PackedGradients(const RasterView& raster) {
    const std::size_t w = raster.width;
    const std::size_t h = raster.height;
    const auto scale = static_cast<float>(0.5 / raster.cellSize);
    std::vector<std::uint32_t> packed(w * h);
    for (std::size_t row = 0; row < h; ++row) {
        const std::size_t north = row > 0 ? row - 1 : row;
        const std::size_t south = row + 1 < h ? row + 1 : row;
        for (std::size_t col = 0; col < w; ++col) {
            const std::size_t west = col > 0 ? col - 1 : col;
            const std::size_t east = col + 1 < w ? col + 1 : col;
            const glm::vec2 gradient((raster(east, row) - raster(west, row)) * scale,
                                     (raster(col, north) - raster(col, south)) * scale);
            packed[row * w + col] = glm::packHalf2x16(gradient);
        }
    }
    return packed;
}

}  // namespace

TerrainRenderer::TerrainRenderer(const VulkanContext& context, VkFormat colorFormat,
                                 VkFormat depthFormat, BrdfRegistry brdfs)
    : context_(context), surface_(context), brdfs_(std::move(brdfs)), colorFormat_(colorFormat),
      depthFormat_(depthFormat), layout_(context, kBindings, sizeof(DrawConstants)),
      uniforms_(context, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                BufferMemory::Upload),
      shadowUniforms_(context, sizeof(ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      BufferMemory::Upload),
      clampSampler_(context, SamplerDesc{}),
      shadowSampler_(context, SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, true}),
      poisson_(PoissonDisk(16)) {
    std::vector<ShaderModuleSource> modules = TerrainModules();
    modules.push_back({"terrain_shadow", shaders::kTerrainShadowSlang});
    const std::vector<std::uint32_t> shadowVertex = compiler_.CompileSpirv(modules, "shadowVertex");
    GraphicsPipelineDesc shadow;
    shadow.vertexSpirv = shadowVertex;
    shadow.depthFormat = VK_FORMAT_D32_SFLOAT;
    shadow.depthBiasSlope = 1.5f;
    shadowPipeline_ = std::make_unique<GraphicsPipeline>(context, layout_, shadow);
    ReservePatches(kInitialPatchCapacity);
    CreateShadowMaps(1);
}

TerrainRenderer::~TerrainRenderer() {
    vkDeviceWaitIdle(context_.Device());
}

std::unique_ptr<GraphicsPipeline>
TerrainRenderer::CreateColorPipeline(const BrdfModel& model) const {
    std::vector<ShaderModuleSource> modules = TerrainModules();
    modules.push_back({BrdfModuleName(model), model.slang});
    modules.push_back({"terrain_color", "import " + BrdfModuleName(model) +
                                            ";\ntypealias ActiveBrdf = " + model.typeName + ";\n" +
                                            shaders::kTerrainColorSlang});
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
    try {
        vertex = compiler_.CompileSpirv(modules, "vertexMain");
        fragment = compiler_.CompileSpirv(modules, "fragmentMain");
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument("BRDF " + model.id + " failed to compile: " + error.what());
    }
    GraphicsPipelineDesc desc;
    desc.vertexSpirv = vertex;
    desc.fragmentSpirv = fragment;
    desc.colorFormat = colorFormat_;
    desc.depthFormat = depthFormat_;
    return std::make_unique<GraphicsPipeline>(context_, layout_, desc);
}

void TerrainRenderer::AddBrdf(BrdfModel model) {
    BrdfRegistry candidate = brdfs_;
    candidate.Add(model);
    auto pipeline = CreateColorPipeline(model);
    colorPipelines_.emplace(model.id, std::move(pipeline));
    brdfs_ = std::move(candidate);
}

void TerrainRenderer::CreateShadowMaps(std::uint32_t resolution) {
    vkDeviceWaitIdle(context_.Device());
    shadowMaps_ = std::make_unique<GpuImage>(
        context_,
        ImageDesc{resolution, resolution, kMaxCascades, 1, VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    context_.Submit([this](VkCommandBuffer commands) {
        shadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    shadowResolution_ = resolution;
}

void TerrainRenderer::ReservePatches(std::size_t count) {
    if (patchBuffer_ != nullptr && count * sizeof(LodPatch) <= patchBuffer_->Size()) {
        return;
    }
    const std::size_t capacity =
        std::max(count, patchBuffer_ != nullptr ? 2 * patchBuffer_->Size() / sizeof(LodPatch)
                                                : std::size_t{0});
    vkDeviceWaitIdle(context_.Device());
    patchBuffer_ =
        std::make_unique<GpuBuffer>(context_, capacity * sizeof(LodPatch),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload);
}

void TerrainRenderer::SetHeightfield(const RasterView& raster) {
    CheckRasterView(raster, "Heightfield", 2);
    const std::uint32_t maxSize = context_.Limits().maxImageDimension2D;
    if (raster.width > maxSize || raster.height > maxSize) {
        throw std::invalid_argument("Heightfield exceeds the device's maximum texture size");
    }

    vkDeviceWaitIdle(context_.Device());
    heights_.reset();
    gradients_.reset();
    tree_ = LodQuadtree();
    patches_.clear();
    mainRange_ = {};
    cascadeRanges_ = {};
    triangles_ = 0;
    updated_ = false;
    width_ = raster.width;
    height_ = raster.height;
    originX_ = raster.originX;
    originY_ = raster.originY;
    pixelSize_ = raster.cellSize;
    float lowest = std::numeric_limits<float>::infinity();
    float highest = -std::numeric_limits<float>::infinity();
    for (std::size_t row = 0; row < height_; ++row) {
        for (std::size_t col = 0; col < width_; ++col) {
            const float value = raster(col, row);
            if (raster.IsValid(value)) {
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
        }
    }
    minHeight_ = lowest <= highest ? lowest : 0.0f;
    maxHeight_ = lowest <= highest ? highest : 0.0f;

    const auto w = static_cast<std::uint32_t>(width_);
    const auto h = static_cast<std::uint32_t>(height_);
    std::vector<float> packedHeights(static_cast<std::size_t>(w) * h);
    for (std::uint32_t row = 0; row < h; ++row) {
        for (std::uint32_t col = 0; col < w; ++col) {
            packedHeights[static_cast<std::size_t>(row) * w + col] = raster(col, row);
        }
    }
    auto heights = std::make_unique<GpuImage>(
        context_, ImageDesc{w, h, 1, 1, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> heightMips[] = {std::as_bytes(std::span(packedHeights))};
    heights->Upload(heightMips);
    const std::vector<std::uint32_t> gradients = PackedGradients(raster);
    auto gradientImage = std::make_unique<GpuImage>(
        context_, ImageDesc{w, h, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> gradientMips[] = {std::as_bytes(std::span(gradients))};
    gradientImage->Upload(gradientMips);
    tree_ = LodQuadtree(raster, kLodLeafCells);
    heights_ = std::move(heights);
    gradients_ = std::move(gradientImage);
}

std::uint64_t TerrainRenderer::GpuBytes() const {
    std::uint64_t bytes = uniforms_.Size() + shadowUniforms_.Size() + patchBuffer_->Size() +
                          shadowMaps_->Bytes() + surface_.GpuBytes();
    if (heights_ != nullptr) {
        bytes += heights_->Bytes() + gradients_->Bytes();
    }
    return bytes;
}

std::pair<glm::vec3, glm::vec3> TerrainRenderer::Bounds(float exaggeration) const {
    const auto halfPixel = static_cast<float>(0.5 * pixelSize_);
    const glm::vec3 low(static_cast<float>(originX_) + halfPixel,
                        static_cast<float>(originY_ - static_cast<double>(height_) * pixelSize_) +
                            halfPixel,
                        minHeight_ * exaggeration);
    const glm::vec3 high(static_cast<float>(originX_ + static_cast<double>(width_) * pixelSize_) -
                             halfPixel,
                         static_cast<float>(originY_) - halfPixel, maxHeight_ * exaggeration);
    return {glm::min(low, high), glm::max(low, high)};
}

void TerrainRenderer::Update(const TerrainDrawParams& params) {
    const BrdfModel* model = brdfs_.Find(params.surface.brdf);
    if (model == nullptr) {
        throw std::invalid_argument("Unknown BRDF: " + params.surface.brdf);
    }
    auto pipeline = colorPipelines_.find(model->id);
    if (pipeline == colorPipelines_.end()) {
        pipeline = colorPipelines_.emplace(model->id, CreateColorPipeline(*model)).first;
    }
    activePipeline_ = pipeline->second.get();
    surface_.Update(params.surface, ResolveBrdfParams(*model, params.surface.brdfParams));

    Uniforms uniforms{};
    const glm::mat4 viewProjection = params.camera.ViewProjection();
    std::memcpy(uniforms.viewProjection, glm::value_ptr(viewProjection),
                sizeof(uniforms.viewProjection));
    uniforms.sun[0] = params.sunDirection.x;
    uniforms.sun[1] = params.sunDirection.y;
    uniforms.sun[2] = params.sunDirection.z;
    uniforms.sun[3] = params.ambient;
    uniforms.grid[0] = static_cast<float>(originX_);
    uniforms.grid[1] = static_cast<float>(originY_);
    uniforms.grid[2] = static_cast<float>(pixelSize_);
    uniforms.grid[3] = params.exaggeration;
    uniforms.heightRange[0] = minHeight_;
    uniforms.heightRange[1] = maxHeight_;
    uniforms.dims[0] = static_cast<std::uint32_t>(width_);
    uniforms.dims[1] = static_cast<std::uint32_t>(height_);
    uniforms.dims[2] = static_cast<std::uint32_t>(params.colorMode);
    uniforms.dims[3] = params.wireframe ? 1u : 0u;
    uniforms.forward[0] = params.camera.forward.x;
    uniforms.forward[1] = params.camera.forward.y;
    uniforms.forward[2] = params.camera.forward.z;
    uniforms.forward[3] = glm::dot(params.camera.eye, params.camera.forward);

    const LodSettings& lod = params.lod;
    if (!(lod.freeze && frozen_)) {
        lodEye_ = params.camera.eye;
        lodFrustum_ = Frustum::FromMatrix(viewProjection);
    }
    frozen_ = lod.freeze;
    std::vector<LodRange> ranges;
    firstRange_ = 0.0f;
    if (lod.enabled && !tree_.Empty()) {
        const float morphStart = std::clamp(lod.morphStart, 0.05f, 0.95f);
        firstRange_ = std::max(lod.detail * tree_.LeafSize(),
                               tree_.MinFirstRange(params.exaggeration, morphStart));
        ranges = tree_.Ranges(firstRange_, morphStart);
    }
    uniforms.eye[0] = params.camera.eye.x;
    uniforms.eye[1] = params.camera.eye.y;
    uniforms.eye[2] = params.camera.eye.z;
    uniforms.lodEye[0] = lodEye_.x;
    uniforms.lodEye[1] = lodEye_.y;
    uniforms.lodEye[2] = lodEye_.z;
    uniforms.lodInfo[0] = tree_.PatchCells();
    uniforms.lodInfo[1] = lod.showLevels ? 1u : 0u;
    for (std::uint32_t level = 0; level < LodQuadtree::kMaxLevels; ++level) {
        // Levels that never morph get a start beyond any distance and a zero slope.
        float start = std::numeric_limits<float>::max();
        float slope = 0.0f;
        if (level < ranges.size() && std::isfinite(ranges[level].morphEnd)) {
            start = ranges[level].morphStart;
            slope = 1.0f / (ranges[level].morphEnd - start);
        }
        uniforms.morph[2 * level] = start;
        uniforms.morph[2 * level + 1] = slope;
    }
    uniforms_.Write(std::as_bytes(std::span(&uniforms, 1)));

    const ShadowSettings& shadows = params.shadows;
    shadowsEnabled_ = shadows.enabled && HasHeightfield() && params.sunDirection.z > 0.0f;
    cascades_.clear();
    if (shadowsEnabled_) {
        if (shadows.resolution != shadowResolution_) {
            CreateShadowMaps(shadows.resolution);
        }
        const auto [low, high] = Bounds(params.exaggeration);
        float farthest = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                   (i & 4) ? high.z : low.z);
            farthest =
                std::max(farthest, glm::dot(corner - params.camera.eye, params.camera.forward));
        }
        const float shadowDistance = std::min(params.camera.farPlane, farthest);
        cascades_ = FitCascades(params.camera, shadowDistance, params.sunDirection, low, high,
                                std::clamp(shadows.cascades, 1, kMaxCascades), shadows.splitLambda,
                                shadowResolution_);
    }

    ShadowUniforms shadowUniforms{};
    for (std::size_t c = 0; c < cascades_.size(); ++c) {
        std::memcpy(shadowUniforms.lightViewProjection[c],
                    glm::value_ptr(cascades_[c].lightViewProjection), 64);
        shadowUniforms.splits[c] = cascades_[c].splitFar;
        shadowUniforms.texelWorld[c] = cascades_[c].texelWorldSize;
    }
    shadowUniforms.params[0] = shadows.softness;
    shadowUniforms.params[1] = shadows.normalOffset;
    shadowUniforms.params[2] = static_cast<float>(cascades_.size());
    shadowUniforms.params[3] = shadowsEnabled_ ? 1.0f : 0.0f;
    shadowUniforms.debug[0] = shadows.showCascades ? 1u : 0u;
    for (std::size_t i = 0; i < poisson_.size(); ++i) {
        shadowUniforms.poisson[2 * i] = poisson_[i].x;
        shadowUniforms.poisson[2 * i + 1] = poisson_[i].y;
    }
    shadowUniforms_.Write(std::as_bytes(std::span(&shadowUniforms, 1)));

    patches_.clear();
    if (ranges.empty()) {
        tree_.SelectAll(patches_);
    } else {
        tree_.Select(lodEye_, ranges, &lodFrustum_, params.exaggeration, patches_);
    }
    mainRange_ = {0, static_cast<std::uint32_t>(patches_.size())};
    triangles_ = tree_.TriangleCount(patches_);
    for (std::size_t c = 0; c < cascades_.size(); ++c) {
        if (ranges.empty()) {
            cascadeRanges_[c] = mainRange_;
            continue;
        }
        const auto first = static_cast<std::uint32_t>(patches_.size());
        const Frustum light = Frustum::FromMatrix(cascades_[c].lightViewProjection);
        tree_.Select(lodEye_, ranges, &light, params.exaggeration, patches_);
        cascadeRanges_[c] = {first, static_cast<std::uint32_t>(patches_.size()) - first};
    }
    if (!patches_.empty()) {
        ReservePatches(patches_.size());
        patchBuffer_->Write(std::as_bytes(std::span(patches_)));
    }
    updated_ = true;
}

std::vector<DescriptorResource> TerrainRenderer::Resources() const {
    std::vector<DescriptorResource> resources = {
        DescriptorResource::Of(uniforms_),           DescriptorResource::Of(heights_->View()),
        DescriptorResource::Of(*patchBuffer_),       DescriptorResource::Of(gradients_->View()),
        DescriptorResource::Of(clampSampler_),       DescriptorResource::Of(shadowUniforms_),
        DescriptorResource::Of(shadowMaps_->View()), DescriptorResource::Of(shadowSampler_),
    };
    surface_.AppendResources(resources);
    return resources;
}

void TerrainRenderer::DrawPatches(VkCommandBuffer commands, DrawRange range,
                                  const glm::mat4& light) const {
    if (range.count == 0) {
        return;
    }
    DrawConstants constants{};
    std::memcpy(constants.lightViewProj, glm::value_ptr(light), sizeof(constants.lightViewProj));
    constants.patchOffset = range.first;
    layout_.PushConstants(commands, &constants, sizeof(constants));
    const std::uint32_t cells = tree_.PatchCells();
    vkCmdDraw(commands, 6 * cells * cells, range.count, 0, 0);
}

void TerrainRenderer::Record(VkCommandBuffer commands, const RenderingTarget& target) {
    const bool drawable = HasHeightfield() && updated_ && activePipeline_ != nullptr;
    if (drawable && shadowsEnabled_) {
        shadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        const std::vector<DescriptorResource> resources = Resources();
        for (std::size_t c = 0; c < cascades_.size(); ++c) {
            RenderingTarget shadow;
            shadow.depth = shadowMaps_->LayerView(static_cast<std::uint32_t>(c));
            shadow.extent = {shadowResolution_, shadowResolution_};
            BeginRendering(commands, shadow);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_->Handle());
            layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, resources);
            DrawPatches(commands, cascadeRanges_[c], cascades_[c].lightViewProjection);
            vkCmdEndRendering(commands);
        }
        shadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    BeginRendering(commands, target);
    if (drawable) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline_->Handle());
        layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Resources());
        DrawPatches(commands, mainRange_, glm::mat4(1.0f));
    }
    vkCmdEndRendering(commands);
}

}  // namespace ernie
