#include "render/terrain_renderer.h"

#include "render/embedded_shaders.h"
#include "render/poisson_disk.h"
#include "terraingen/core/raster_stats.h"

#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen::render {

namespace {

constexpr std::uint64_t kLightStride = 256;
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
static_assert(sizeof(Uniforms) == 320, "Uniforms must match the WGSL layout");
static_assert(sizeof(LodPatch) == 16, "LodPatch must match the WGSL vec4<u32>");

struct ShadowUniforms {
    float lightViewProjection[TerrainRenderer::kMaxCascades][16];
    float splits[4];
    float texelWorld[4];
    float params[4];
    std::uint32_t debug[4];
    float poisson[32];
};
static_assert(sizeof(ShadowUniforms) == 448, "ShadowUniforms must match the WGSL layout");

WGPUStringView View(const char* text) {
    return WGPUStringView{text, WGPU_STRLEN};
}

WGPUShaderModule CreateModule(WGPUDevice device, const std::string& source, const char* label) {
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = WGPUStringView{source.data(), source.size()};
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &wgsl.chain;
    descriptor.label = View(label);
    return wgpuDeviceCreateShaderModule(device, &descriptor);
}

WGPUBindGroupLayout CreateLayout(WGPUDevice device, const WGPUBindGroupLayoutEntry* entries,
                                 std::size_t count) {
    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.entryCount = count;
    descriptor.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &descriptor);
}

WGPUBuffer CreateUniformBuffer(WGPUDevice device, std::uint64_t size) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(device, &descriptor);
}

WGPUPipelineLayout CreatePipelineLayout(WGPUDevice device, const WGPUBindGroupLayout* layouts,
                                        std::size_t count) {
    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.bindGroupLayoutCount = count;
    descriptor.bindGroupLayouts = layouts;
    return wgpuDeviceCreatePipelineLayout(device, &descriptor);
}

// Central-difference gradients (dz/dx east, dz/dy north) per cell as packed half floats, clamping
// at the raster's edges.
std::vector<std::uint32_t> PackedGradients(const Raster<float>& raster, double pixelSize) {
    const std::size_t w = raster.Width();
    const std::size_t h = raster.Height();
    const auto scale = static_cast<float>(0.5 / pixelSize);
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

TerrainRenderer::TerrainRenderer(const GpuContext& gpu, WGPUTextureFormat colorFormat,
                                 WGPUTextureFormat depthFormat, BrdfRegistry brdfs)
    : gpu_(gpu), surface_(gpu), brdfs_(std::move(brdfs)), colorFormat_(colorFormat),
      depthFormat_(depthFormat), poisson_(PoissonDisk(16)) {
    WGPUDevice device = gpu.Device();

    WGPUBindGroupLayoutEntry terrainEntries[5] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    terrainEntries[0].binding = 0;
    terrainEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    terrainEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    terrainEntries[0].buffer.minBindingSize = sizeof(Uniforms);
    terrainEntries[1].binding = 1;
    terrainEntries[1].visibility = WGPUShaderStage_Vertex;
    terrainEntries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    terrainEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    terrainEntries[2].binding = 2;
    terrainEntries[2].visibility = WGPUShaderStage_Vertex;
    terrainEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    terrainEntries[2].buffer.minBindingSize = sizeof(LodPatch);
    terrainEntries[3].binding = 3;
    terrainEntries[3].visibility = WGPUShaderStage_Fragment;
    terrainEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
    terrainEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
    terrainEntries[4].binding = 4;
    terrainEntries[4].visibility = WGPUShaderStage_Fragment;
    terrainEntries[4].sampler.type = WGPUSamplerBindingType_Filtering;
    terrainLayout_ = CreateLayout(device, terrainEntries, 5);

    WGPUBindGroupLayoutEntry shadowEntries[3] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                 WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                 WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    shadowEntries[0].binding = 0;
    shadowEntries[0].visibility = WGPUShaderStage_Fragment;
    shadowEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    shadowEntries[0].buffer.minBindingSize = sizeof(ShadowUniforms);
    shadowEntries[1].binding = 1;
    shadowEntries[1].visibility = WGPUShaderStage_Fragment;
    shadowEntries[1].texture.sampleType = WGPUTextureSampleType_Depth;
    shadowEntries[1].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    shadowEntries[2].binding = 2;
    shadowEntries[2].visibility = WGPUShaderStage_Fragment;
    shadowEntries[2].sampler.type = WGPUSamplerBindingType_Comparison;
    shadowSamplingLayout_ = CreateLayout(device, shadowEntries, 3);

    WGPUBindGroupLayoutEntry lightEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    lightEntry.binding = 0;
    lightEntry.visibility = WGPUShaderStage_Vertex;
    lightEntry.buffer.type = WGPUBufferBindingType_Uniform;
    lightEntry.buffer.hasDynamicOffset = WGPU_TRUE;
    lightEntry.buffer.minBindingSize = 64;
    lightLayout_ = CreateLayout(device, &lightEntry, 1);

    const std::string common = shaders::kTerrainCommonWgsl;
    WGPUShaderModule shadowModule =
        CreateModule(device, common + shaders::kTerrainShadowWgsl, "terrain shadow");
    const WGPUBindGroupLayout colorLayouts[3] = {terrainLayout_, shadowSamplingLayout_,
                                                 surface_.Layout()};
    colorLayout_ = CreatePipelineLayout(device, colorLayouts, 3);

    const WGPUBindGroupLayout shadowLayouts[2] = {terrainLayout_, lightLayout_};
    WGPUPipelineLayout shadowLayout = CreatePipelineLayout(device, shadowLayouts, 2);
    WGPUDepthStencilState shadowDepth = WGPU_DEPTH_STENCIL_STATE_INIT;
    shadowDepth.format = WGPUTextureFormat_Depth32Float;
    shadowDepth.depthWriteEnabled = WGPUOptionalBool_True;
    shadowDepth.depthCompare = WGPUCompareFunction_Less;
    shadowDepth.depthBiasSlopeScale = 1.5f;
    WGPURenderPipelineDescriptor shadowDescriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    shadowDescriptor.label = View("terrain shadow");
    shadowDescriptor.layout = shadowLayout;
    shadowDescriptor.vertex.module = shadowModule;
    shadowDescriptor.vertex.entryPoint = View("vs_shadow");
    shadowDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    shadowDescriptor.primitive.cullMode = WGPUCullMode_None;
    shadowDescriptor.depthStencil = &shadowDepth;
    shadowPipeline_ = wgpuDeviceCreateRenderPipeline(device, &shadowDescriptor);

    wgpuPipelineLayoutRelease(shadowLayout);
    wgpuShaderModuleRelease(shadowModule);

    uniformBuffer_ = CreateUniformBuffer(device, sizeof(Uniforms));
    shadowUniformBuffer_ = CreateUniformBuffer(device, sizeof(ShadowUniforms));
    lightBuffer_ = CreateUniformBuffer(device, kLightStride * kMaxCascades);

    WGPUBindGroupEntry lightBinding = WGPU_BIND_GROUP_ENTRY_INIT;
    lightBinding.binding = 0;
    lightBinding.buffer = lightBuffer_;
    lightBinding.size = 64;
    WGPUBindGroupDescriptor lightGroup = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    lightGroup.layout = lightLayout_;
    lightGroup.entryCount = 1;
    lightGroup.entries = &lightBinding;
    lightBindGroup_ = wgpuDeviceCreateBindGroup(device, &lightGroup);

    WGPUSamplerDescriptor samplerDescriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDescriptor.magFilter = WGPUFilterMode_Linear;
    samplerDescriptor.minFilter = WGPUFilterMode_Linear;
    samplerDescriptor.compare = WGPUCompareFunction_Less;
    shadowSampler_ = wgpuDeviceCreateSampler(device, &samplerDescriptor);

    WGPUSamplerDescriptor gradientSampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    gradientSampler.magFilter = WGPUFilterMode_Linear;
    gradientSampler.minFilter = WGPUFilterMode_Linear;
    gradientSampler_ = wgpuDeviceCreateSampler(device, &gradientSampler);

    ReservePatches(kInitialPatchCapacity);
    CreateShadowMaps(1);
}

TerrainRenderer::~TerrainRenderer() {
    ReleaseHeightfield();
    ReleaseShadowMaps();
    wgpuBufferRelease(patchBuffer_);
    wgpuSamplerRelease(gradientSampler_);
    wgpuSamplerRelease(shadowSampler_);
    wgpuBindGroupRelease(lightBindGroup_);
    wgpuBufferRelease(lightBuffer_);
    wgpuBufferRelease(shadowUniformBuffer_);
    wgpuBufferRelease(uniformBuffer_);
    wgpuRenderPipelineRelease(shadowPipeline_);
    for (const auto& [id, pipeline] : colorPipelines_) {
        wgpuRenderPipelineRelease(pipeline);
    }
    wgpuPipelineLayoutRelease(colorLayout_);
    wgpuBindGroupLayoutRelease(lightLayout_);
    wgpuBindGroupLayoutRelease(shadowSamplingLayout_);
    wgpuBindGroupLayoutRelease(terrainLayout_);
}

WGPURenderPipeline TerrainRenderer::CreateColorPipeline(const BrdfModel& model) const {
    WGPUDevice device = gpu_.Device();
    const std::string source = std::string(shaders::kTerrainCommonWgsl) +
                               std::string(BrdfCommonWgsl()) + "\n" + model.wgsl + "\n" +
                               shaders::kSurfaceWgsl + shaders::kTerrainWgsl;
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    WGPUShaderModule module = CreateModule(device, source, "terrain");
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = depthFormat_;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Less;
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = colorFormat_;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = View("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = View("terrain");
    descriptor.layout = colorLayout_;
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = View("vs_main");
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.primitive.cullMode = WGPUCullMode_None;
    descriptor.depthStencil = &depthStencil;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    wgpuShaderModuleRelease(module);

    struct ScopeResult {
        bool done = false;
        WGPUErrorType type = WGPUErrorType_NoError;
        std::string message;
    } result;
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView message,
                           void* userdata1, void*) {
        auto* out = static_cast<ScopeResult*>(userdata1);
        out->type = type;
        if (message.data != nullptr) {
            out->message = message.length == WGPU_STRLEN
                               ? std::string(message.data)
                               : std::string(message.data, message.length);
        }
        out->done = true;
    };
    callback.userdata1 = &result;
    wgpuDevicePopErrorScope(device, callback);
    gpu_.WaitUntil([&result] { return result.done; });
    if (result.type != WGPUErrorType_NoError) {
        wgpuRenderPipelineRelease(pipeline);
        throw std::invalid_argument("BRDF " + model.id + " failed to compile: " + result.message);
    }
    return pipeline;
}

void TerrainRenderer::AddBrdf(BrdfModel model) {
    BrdfRegistry candidate = brdfs_;
    candidate.Add(model);
    const WGPURenderPipeline pipeline = CreateColorPipeline(model);
    colorPipelines_.emplace(model.id, pipeline);
    brdfs_ = std::move(candidate);
}

void TerrainRenderer::CreateShadowMaps(std::uint32_t resolution) {
    ReleaseShadowMaps();
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = View("shadow maps");
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = {resolution, resolution, kMaxCascades};
    descriptor.format = WGPUTextureFormat_Depth32Float;
    shadowTexture_ = wgpuDeviceCreateTexture(gpu_.Device(), &descriptor);

    WGPUTextureViewDescriptor arrayView = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    arrayView.dimension = WGPUTextureViewDimension_2DArray;
    arrayView.arrayLayerCount = kMaxCascades;
    shadowArrayView_ = wgpuTextureCreateView(shadowTexture_, &arrayView);
    for (std::uint32_t layer = 0; layer < kMaxCascades; ++layer) {
        WGPUTextureViewDescriptor layerView = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        layerView.dimension = WGPUTextureViewDimension_2D;
        layerView.baseArrayLayer = layer;
        layerView.arrayLayerCount = 1;
        shadowLayerViews_[layer] = wgpuTextureCreateView(shadowTexture_, &layerView);
    }

    WGPUBindGroupEntry entries[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = shadowUniformBuffer_;
    entries[0].size = sizeof(ShadowUniforms);
    entries[1].binding = 1;
    entries[1].textureView = shadowArrayView_;
    entries[2].binding = 2;
    entries[2].sampler = shadowSampler_;
    WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group.layout = shadowSamplingLayout_;
    group.entryCount = 3;
    group.entries = entries;
    shadowBindGroup_ = wgpuDeviceCreateBindGroup(gpu_.Device(), &group);
    shadowResolution_ = resolution;
}

void TerrainRenderer::ReleaseShadowMaps() {
    if (shadowBindGroup_ != nullptr) {
        wgpuBindGroupRelease(shadowBindGroup_);
        shadowBindGroup_ = nullptr;
    }
    for (WGPUTextureView& view : shadowLayerViews_) {
        if (view != nullptr) {
            wgpuTextureViewRelease(view);
            view = nullptr;
        }
    }
    if (shadowArrayView_ != nullptr) {
        wgpuTextureViewRelease(shadowArrayView_);
        shadowArrayView_ = nullptr;
    }
    if (shadowTexture_ != nullptr) {
        wgpuTextureRelease(shadowTexture_);
        shadowTexture_ = nullptr;
    }
    shadowResolution_ = 0;
}

void TerrainRenderer::SetHeightfield(const Raster<float>& raster) {
    const GeoTransform& t = raster.Transform();
    if (raster.Width() < 2 || raster.Height() < 2) {
        throw std::invalid_argument("Heightfield must be at least 2 x 2 cells");
    }
    if (t.rowRotation != 0.0 || t.colRotation != 0.0 || !(t.pixelWidth > 0.0) ||
        t.pixelHeight != -t.pixelWidth) {
        throw std::invalid_argument("Heightfield must be north-up with square pixels");
    }
    WGPULimits limits = WGPU_LIMITS_INIT;
    wgpuDeviceGetLimits(gpu_.Device(), &limits);
    if (raster.Width() > limits.maxTextureDimension2D ||
        raster.Height() > limits.maxTextureDimension2D) {
        throw std::invalid_argument("Heightfield exceeds the device's maximum texture size");
    }

    ReleaseHeightfield();
    width_ = raster.Width();
    height_ = raster.Height();
    originX_ = t.originX;
    originY_ = t.originY;
    pixelSize_ = t.pixelWidth;
    const RasterStats stats = ComputeStats(raster);
    minHeight_ = stats.validCount > 0 ? static_cast<float>(stats.min) : 0.0f;
    maxHeight_ = stats.validCount > 0 ? static_cast<float>(stats.max) : 0.0f;

    const auto w = static_cast<std::uint32_t>(width_);
    const auto h = static_cast<std::uint32_t>(height_);
    WGPUTextureDescriptor textureDescriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDescriptor.label = View("heights");
    textureDescriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    textureDescriptor.dimension = WGPUTextureDimension_2D;
    textureDescriptor.size = {w, h, 1};
    textureDescriptor.format = WGPUTextureFormat_R32Float;
    heightTexture_ = wgpuDeviceCreateTexture(gpu_.Device(), &textureDescriptor);
    heightView_ = wgpuTextureCreateView(heightTexture_, nullptr);

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = heightTexture_;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = w * sizeof(float);
    layout.rowsPerImage = h;
    const WGPUExtent3D size{w, h, 1};
    wgpuQueueWriteTexture(gpu_.Queue(), &destination, raster.Values().data(),
                          raster.Size() * sizeof(float), &layout, &size);

    textureDescriptor.label = View("height gradients");
    textureDescriptor.format = WGPUTextureFormat_RG16Float;
    gradientTexture_ = wgpuDeviceCreateTexture(gpu_.Device(), &textureDescriptor);
    gradientView_ = wgpuTextureCreateView(gradientTexture_, nullptr);
    destination.texture = gradientTexture_;
    const std::vector<std::uint32_t> gradients = PackedGradients(raster, pixelSize_);
    wgpuQueueWriteTexture(gpu_.Queue(), &destination, gradients.data(),
                          gradients.size() * sizeof(std::uint32_t), &layout, &size);

    tree_ = LodQuadtree(raster, kLodLeafCells);
    CreateTerrainBindGroup();
}

void TerrainRenderer::CreateTerrainBindGroup() {
    if (terrainBindGroup_ != nullptr) {
        wgpuBindGroupRelease(terrainBindGroup_);
        terrainBindGroup_ = nullptr;
    }
    if (heightView_ == nullptr) {
        return;
    }
    WGPUBindGroupEntry entries[5] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = uniformBuffer_;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].textureView = heightView_;
    entries[2].binding = 2;
    entries[2].buffer = patchBuffer_;
    entries[2].size = patchCapacity_ * sizeof(LodPatch);
    entries[3].binding = 3;
    entries[3].textureView = gradientView_;
    entries[4].binding = 4;
    entries[4].sampler = gradientSampler_;
    WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group.layout = terrainLayout_;
    group.entryCount = 5;
    group.entries = entries;
    terrainBindGroup_ = wgpuDeviceCreateBindGroup(gpu_.Device(), &group);
}

void TerrainRenderer::ReservePatches(std::size_t count) {
    if (count <= patchCapacity_) {
        return;
    }
    if (patchBuffer_ != nullptr) {
        wgpuBufferRelease(patchBuffer_);
    }
    patchCapacity_ = std::max(count, 2 * patchCapacity_);
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = View("lod patches");
    descriptor.size = patchCapacity_ * sizeof(LodPatch);
    descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    patchBuffer_ = wgpuDeviceCreateBuffer(gpu_.Device(), &descriptor);
    CreateTerrainBindGroup();
}

std::uint64_t TerrainRenderer::GpuBytes() const {
    const std::uint64_t shadowBytes = static_cast<std::uint64_t>(shadowResolution_) *
                                      shadowResolution_ * kMaxCascades * sizeof(float);
    const std::uint64_t cells = static_cast<std::uint64_t>(width_) * height_;
    return cells * (sizeof(float) + sizeof(std::uint32_t)) + shadowBytes + sizeof(Uniforms) +
           sizeof(ShadowUniforms) + kLightStride * kMaxCascades +
           patchCapacity_ * sizeof(LodPatch) + surface_.GpuBytes();
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
    activePipeline_ = pipeline->second;
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
    wgpuQueueWriteBuffer(gpu_.Queue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

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
        wgpuQueueWriteBuffer(gpu_.Queue(), lightBuffer_, kLightStride * c,
                             glm::value_ptr(cascades_[c].lightViewProjection), 64);
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
    wgpuQueueWriteBuffer(gpu_.Queue(), shadowUniformBuffer_, 0, &shadowUniforms,
                         sizeof(shadowUniforms));

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
        wgpuQueueWriteBuffer(gpu_.Queue(), patchBuffer_, 0, patches_.data(),
                             patches_.size() * sizeof(LodPatch));
    }
}

void TerrainRenderer::EncodeShadows(WGPUCommandEncoder encoder) const {
    if (!shadowsEnabled_) {
        return;
    }
    for (std::size_t c = 0; c < cascades_.size(); ++c) {
        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depth.view = shadowLayerViews_[c];
        depth.depthLoadOp = WGPULoadOp_Clear;
        depth.depthStoreOp = WGPUStoreOp_Store;
        depth.depthClearValue = 1.0f;
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.label = View("shadow cascade");
        descriptor.depthStencilAttachment = &depth;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
        wgpuRenderPassEncoderSetPipeline(pass, shadowPipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, terrainBindGroup_, 0, nullptr);
        const auto offset = static_cast<std::uint32_t>(kLightStride * c);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, lightBindGroup_, 1, &offset);
        DrawPatches(pass, cascadeRanges_[c]);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
}

void TerrainRenderer::Draw(WGPURenderPassEncoder pass) const {
    if (!HasHeightfield()) {
        return;
    }
    if (activePipeline_ == nullptr) {
        return;
    }
    wgpuRenderPassEncoderSetPipeline(pass, activePipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, terrainBindGroup_, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, shadowBindGroup_, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, surface_.BindGroup(), 0, nullptr);
    DrawPatches(pass, mainRange_);
}

void TerrainRenderer::DrawPatches(WGPURenderPassEncoder pass, DrawRange range) const {
    if (range.count == 0) {
        return;
    }
    const std::uint32_t cells = tree_.PatchCells();
    wgpuRenderPassEncoderDraw(pass, 6 * cells * cells, range.count, 0, range.first);
}

void TerrainRenderer::ReleaseHeightfield() {
    if (terrainBindGroup_ != nullptr) {
        wgpuBindGroupRelease(terrainBindGroup_);
        terrainBindGroup_ = nullptr;
    }
    if (heightView_ != nullptr) {
        wgpuTextureViewRelease(heightView_);
        heightView_ = nullptr;
    }
    if (heightTexture_ != nullptr) {
        wgpuTextureRelease(heightTexture_);
        heightTexture_ = nullptr;
    }
    if (gradientView_ != nullptr) {
        wgpuTextureViewRelease(gradientView_);
        gradientView_ = nullptr;
    }
    if (gradientTexture_ != nullptr) {
        wgpuTextureRelease(gradientTexture_);
        gradientTexture_ = nullptr;
    }
    tree_ = LodQuadtree();
    patches_.clear();
    mainRange_ = {};
    cascadeRanges_ = {};
    triangles_ = 0;
    width_ = 0;
    height_ = 0;
}

}  // namespace terraingen::render
