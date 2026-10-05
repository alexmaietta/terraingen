#include "ernie/raster/terrain_part.h"

#include "core/raster_checks.h"
#include "embedded_shaders.h"

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

struct TerrainUniforms {
    float grid[4];
    float heightRange[4];
    std::uint32_t dims[4];
    float lodEye[4];
    std::uint32_t lodInfo[4];
    float morph[LodQuadtree::kMaxLevels * 2];
    std::uint32_t ids[4];
};
static_assert(sizeof(TerrainUniforms) == 224, "TerrainUniforms must match terrain.slang");
static_assert(sizeof(LodPatch) == 16, "LodPatch must match the shaders' uint4");

struct DrawConstants {
    float lightViewProj[16];
    std::uint32_t first;
    std::uint32_t pad[3];
};
static_assert(sizeof(DrawConstants) == 80, "DrawConstants must match frame.slang");

constexpr VkShaderStageFlags kGraphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

/// Frame (0 to 4), material (5 to 9) and terrain (10 to 13) bindings; see terrain.slang.
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
    {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGraphics},
    {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
    {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGraphics},
    {13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGraphics},
};

std::vector<ShaderModuleSource> TerrainModules() {
    return {{"brdf", std::string(BrdfSlang())},
            {"frame", shaders::kFrameSlang},
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

TerrainPart::TerrainPart(RasterRenderer& renderer)
    : renderer_(renderer), layout_(renderer.Context(), kBindings, sizeof(DrawConstants)),
      uniforms_(renderer.Context(), sizeof(TerrainUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                BufferMemory::Upload) {
    std::vector<ShaderModuleSource> modules = TerrainModules();
    modules.push_back({"terrain_shadow", shaders::kTerrainShadowSlang});
    const std::vector<std::uint32_t> shadowVertex =
        renderer_.Compiler().CompileSpirv(modules, "shadowVertex");
    GraphicsPipelineDesc shadow;
    shadow.vertexSpirv = shadowVertex;
    shadow.depthFormat = VK_FORMAT_D32_SFLOAT;
    shadow.depthBiasSlope = 1.5f;
    shadowPipeline_ = std::make_unique<GraphicsPipeline>(renderer_.Context(), layout_, shadow);
    CreateGBufferPipeline();
    ReservePatches(kInitialPatchCapacity);
}

TerrainPart::~TerrainPart() {
    vkDeviceWaitIdle(renderer_.Context().Device());
}

void TerrainPart::CreateGBufferPipeline() {
    std::vector<ShaderModuleSource> modules = TerrainModules();
    modules.push_back({"terrain_color", shaders::kTerrainColorSlang});
    const std::vector<std::uint32_t> vertex =
        renderer_.Compiler().CompileSpirv(modules, "vertexMain");
    const std::vector<std::uint32_t> fragment =
        renderer_.Compiler().CompileSpirv(modules, "fragmentMain");
    GraphicsPipelineDesc desc;
    desc.vertexSpirv = vertex;
    desc.fragmentSpirv = fragment;
    desc.colorFormats.assign(RenderTargets::kGBufferFormats.begin(),
                             RenderTargets::kGBufferFormats.end());
    desc.depthFormat = RenderTargets::kDepthFormat;
    gbufferPipeline_ = std::make_unique<GraphicsPipeline>(renderer_.Context(), layout_, desc);
}

void TerrainPart::ReservePatches(std::size_t count) {
    if (patchBuffer_ != nullptr && count * sizeof(LodPatch) <= patchBuffer_->Size()) {
        return;
    }
    const std::size_t capacity =
        std::max(count, patchBuffer_ != nullptr ? 2 * patchBuffer_->Size() / sizeof(LodPatch)
                                                : std::size_t{0});
    vkDeviceWaitIdle(renderer_.Context().Device());
    patchBuffer_ =
        std::make_unique<GpuBuffer>(renderer_.Context(), capacity * sizeof(LodPatch),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload);
}

void TerrainPart::ClearHeightfield() {
    vkDeviceWaitIdle(renderer_.Context().Device());
    heights_.reset();
    gradients_.reset();
    tree_ = LodQuadtree();
    patches_.clear();
    mainRange_ = {};
    cascadeRanges_.clear();
    triangles_ = 0;
    prepared_ = false;
    width_ = 0;
    height_ = 0;
}

void TerrainPart::SetHeightfield(const RasterView& raster) {
    CheckRasterView(raster, "Heightfield", 2);
    const std::uint32_t maxSize = renderer_.Context().Limits().maxImageDimension2D;
    if (raster.width > maxSize || raster.height > maxSize) {
        throw std::invalid_argument("Heightfield exceeds the device's maximum texture size");
    }

    vkDeviceWaitIdle(renderer_.Context().Device());
    heights_.reset();
    gradients_.reset();
    tree_ = LodQuadtree();
    patches_.clear();
    mainRange_ = {};
    cascadeRanges_.clear();
    triangles_ = 0;
    prepared_ = false;
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
    auto heights =
        std::make_unique<GpuImage>(renderer_.Context(), ImageDesc{w, h, 1, 1, VK_FORMAT_R32_SFLOAT,
                                                                  VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> heightMips[] = {std::as_bytes(std::span(packedHeights))};
    heights->Upload(heightMips);
    const std::vector<std::uint32_t> gradients = PackedGradients(raster);
    auto gradientImage = std::make_unique<GpuImage>(
        renderer_.Context(),
        ImageDesc{w, h, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> gradientMips[] = {std::as_bytes(std::span(gradients))};
    gradientImage->Upload(gradientMips);
    tree_ = LodQuadtree(raster, kLodLeafCells);
    heights_ = std::move(heights);
    gradients_ = std::move(gradientImage);
}

std::uint64_t TerrainPart::GpuBytes() const {
    std::uint64_t bytes = uniforms_.Size() + patchBuffer_->Size();
    if (heights_ != nullptr) {
        bytes += heights_->Bytes() + gradients_->Bytes();
    }
    return bytes;
}

std::pair<glm::vec3, glm::vec3> TerrainPart::Bounds(float exaggeration) const {
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

std::optional<Bounds3> TerrainPart::Bounds() const {
    if (!HasHeightfield()) {
        return std::nullopt;
    }
    return Bounds(params_.exaggeration);
}

void TerrainPart::Prepare(const FrameParams& frame, std::span<const ShadowCascade> cascades) {
    prepared_ = false;
    if (!HasHeightfield()) {
        return;
    }
    const std::uint32_t material = renderer_.HasMaterial(params_.material)
                                       ? params_.material
                                       : RasterRenderer::kDefaultMaterial;
    TerrainUniforms uniforms{};
    uniforms.grid[0] = static_cast<float>(originX_);
    uniforms.grid[1] = static_cast<float>(originY_);
    uniforms.grid[2] = static_cast<float>(pixelSize_);
    uniforms.grid[3] = params_.exaggeration;
    uniforms.heightRange[0] = minHeight_;
    uniforms.heightRange[1] = maxHeight_;
    uniforms.dims[0] = static_cast<std::uint32_t>(width_);
    uniforms.dims[1] = static_cast<std::uint32_t>(height_);
    uniforms.dims[2] = static_cast<std::uint32_t>(params_.colorMode);
    uniforms.dims[3] = params_.lod.showLevels ? 1u : 0u;

    const LodSettings& lod = params_.lod;
    const CameraView view = frame.View();
    const glm::mat4 viewProjection = view.ViewProjection();
    if (!(lod.freeze && frozen_)) {
        lodEye_ = view.eye;
        lodFrustum_ = Frustum::FromMatrix(viewProjection);
    }
    frozen_ = lod.freeze;
    std::vector<LodRange> ranges;
    firstRange_ = 0.0f;
    if (lod.enabled && !tree_.Empty()) {
        const float morphStart = std::clamp(lod.morphStart, 0.05f, 0.95f);
        firstRange_ = std::max(lod.detail * tree_.LeafSize(),
                               tree_.MinFirstRange(params_.exaggeration, morphStart));
        ranges = tree_.Ranges(firstRange_, morphStart);
    }
    uniforms.lodEye[0] = lodEye_.x;
    uniforms.lodEye[1] = lodEye_.y;
    uniforms.lodEye[2] = lodEye_.z;
    uniforms.lodInfo[0] = tree_.PatchCells();
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
    uniforms.ids[0] = material;
    uniforms.ids[1] = params_.primId;
    uniforms_.Write(std::as_bytes(std::span(&uniforms, 1)));

    patches_.clear();
    if (ranges.empty()) {
        tree_.SelectAll(patches_);
    } else {
        tree_.Select(lodEye_, ranges, &lodFrustum_, params_.exaggeration, patches_);
    }
    mainRange_ = {0, static_cast<std::uint32_t>(patches_.size())};
    triangles_ = tree_.TriangleCount(patches_);
    cascadeRanges_.assign(cascades.size(), DrawRange{});
    for (std::size_t c = 0; c < cascades.size(); ++c) {
        if (ranges.empty()) {
            cascadeRanges_[c] = mainRange_;
            continue;
        }
        const auto first = static_cast<std::uint32_t>(patches_.size());
        const Frustum light = Frustum::FromMatrix(cascades[c].lightViewProjection);
        tree_.Select(lodEye_, ranges, &light, params_.exaggeration, patches_);
        cascadeRanges_[c] = {first, static_cast<std::uint32_t>(patches_.size()) - first};
    }
    if (!patches_.empty()) {
        ReservePatches(patches_.size());
        patchBuffer_->Write(std::as_bytes(std::span(patches_)));
    }
    prepared_ = true;
}

std::vector<DescriptorResource> TerrainPart::Resources(const RasterFrame& frame) const {
    std::vector<DescriptorResource> resources = {frame.uniforms, frame.shadows, frame.shadowMap,
                                                 frame.shadowSampler, frame.clampSampler};
    renderer_.MaterialOrDefault(params_.material).AppendResources(resources);
    resources.push_back(DescriptorResource::Of(uniforms_));
    resources.push_back(DescriptorResource::Of(heights_->View()));
    resources.push_back(DescriptorResource::Of(*patchBuffer_));
    resources.push_back(DescriptorResource::Of(gradients_->View()));
    return resources;
}

void TerrainPart::DrawPatches(VkCommandBuffer commands, DrawRange range,
                              const glm::mat4& light) const {
    if (range.count == 0) {
        return;
    }
    DrawConstants constants{};
    std::memcpy(constants.lightViewProj, glm::value_ptr(light), sizeof(constants.lightViewProj));
    constants.first = range.first;
    layout_.PushConstants(commands, &constants, sizeof(constants));
    const std::uint32_t cells = tree_.PatchCells();
    vkCmdDraw(commands, 6 * cells * cells, range.count, 0, 0);
}

void TerrainPart::RecordShadow(VkCommandBuffer commands, const RasterFrame& frame,
                               std::size_t cascade, const glm::mat4& lightViewProjection) const {
    if (!prepared_ || cascade >= cascadeRanges_.size()) {
        return;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_->Handle());
    layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Resources(frame));
    DrawPatches(commands, cascadeRanges_[cascade], lightViewProjection);
}

void TerrainPart::RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const {
    if (!prepared_) {
        return;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_->Handle());
    layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Resources(frame));
    DrawPatches(commands, mainRange_, glm::mat4(1.0f));
}

}  // namespace ernie
