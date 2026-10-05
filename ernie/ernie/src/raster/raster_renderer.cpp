#include "ernie/raster/raster_renderer.h"

#include "embedded_shaders.h"
#include "ernie/raster/poisson_disk.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace ernie {

namespace {

struct Uniforms {
    float viewProjection[16];
    float sun[4];
    float forward[4];
    float eye[4];
    std::uint32_t flags[4];
};
static_assert(sizeof(Uniforms) == 128, "Uniforms must match frame.slang");

struct ShadowUniforms {
    float lightViewProjection[4][16];
    float splits[4];
    float texelWorld[4];
    float depthWorld[4];
    float params[4];
    float sun[4];
    std::uint32_t debug[4];
    float poisson[32];
};
static_assert(sizeof(ShadowUniforms) == 480, "ShadowUniforms must match frame.slang");

struct ShadingUniforms {
    float invViewProj[16];
    float eye[4];
    float sun[4];
    float range[4];
    std::uint32_t size[4];
    float irradiance[SpectralBands::kMaxBands];
    float lightShadow[4];
    float poisson[32];
    float domeSh[9][4];
    float dome[SpectralBands::kMaxBands];
};
static_assert(sizeof(ShadingUniforms) == 928, "ShadingUniforms must match shading.slang");

struct LightGpu {
    float position[4];
    float axis[4];
    float cone[4];
    float shadow[4];
    float extentX[4];
    float extentY[4];
    float spectrum[SpectralBands::kMaxBands];
};
static_assert(sizeof(LightGpu) == 352, "LightGpu must match shading.slang");

struct MaterialGpu {
    std::uint32_t info[4];
    float params[BrdfRegistry::kMaxParams];
    float reflectance[SpectralBands::kMaxBands];
};
static_assert(sizeof(MaterialGpu) == 336, "MaterialGpu must match shading.slang");

/// Reflectance luminance below which a material counts as black; its spectrum's shape is flat.
constexpr float kBlackLuminance = 1e-6f;

struct SensorUniforms {
    float invViewProj[16];
    float eye[4];
    float forward[4];
    float intrinsics[4];
    std::uint32_t size[4];
    float distortion[4];
    float lens[4];
    float sensor[4];
    float adc[4];
    std::uint32_t noise[4];
    float clear[4];
    float colour[SpectralBands::kMaxBands][4];
    float qe[SpectralBands::kMaxBands][4];
};
static_assert(sizeof(SensorUniforms) == 2272, "SensorUniforms must match filters.slang");

/// Samples per axis of the RGB-to-spectrum coefficient table.
constexpr std::uint32_t kTableResolution = 17;

constexpr VkShaderStageFlags kCompute = VK_SHADER_STAGE_COMPUTE_BIT;

constexpr DescriptorBinding kShadingBindings[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kCompute},
    {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCompute},
    {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {7, VK_DESCRIPTOR_TYPE_SAMPLER, kCompute},
    {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCompute},
    {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {11, VK_DESCRIPTOR_TYPE_SAMPLER, kCompute},
    {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCompute},
};

constexpr DescriptorBinding kFilterBindings[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kCompute},
    {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
    {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
    {14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kCompute},
};

constexpr DescriptorBinding kPresentBindings[] = {
    {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT},
};

constexpr std::array<const char*, 6> kFilterEntries{"lensMain", "qeMain",  "noiseMain",
                                                    "adcMain",  "cieMain", "exposureMain"};

/// Jakob-Hanika coefficients over a `kTableResolution`^3 grid of linear RGB, red along x, green
/// along y, blue across layers, as RGBA32F texels; computed once per process.
const std::vector<float>& SigmoidTable() {
    static const std::vector<float> table = [] {
        const std::uint32_t n = kTableResolution;
        std::vector<float> texels(static_cast<std::size_t>(n) * n * n * 4);
        for (std::uint32_t b = 0; b < n; ++b) {
            for (std::uint32_t g = 0; g < n; ++g) {
                for (std::uint32_t r = 0; r < n; ++r) {
                    const glm::dvec3 rgb(r / double(n - 1), g / double(n - 1), b / double(n - 1));
                    const glm::dvec3 c = RgbToSigmoid(rgb).coefficients;
                    float* texel = &texels[((static_cast<std::size_t>(b) * n + g) * n + r) * 4];
                    texel[0] = static_cast<float>(c.x);
                    texel[1] = static_cast<float>(c.y);
                    texel[2] = static_cast<float>(c.z);
                    texel[3] = 0.0f;
                }
            }
        }
        return texels;
    }();
    return table;
}

/// A barrier making compute and graphics writes visible to later compute, fragment and transfer
/// reads.
void ShaderBarrier(VkCommandBuffer commands) {
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

/// The integral of the CIE y function over 360-830 nm, in nm.
double LuminanceIntegral() {
    static const double integral = [] {
        double sum = 0.0;
        for (double w = 360.0; w <= 830.0; w += 1.0) {
            sum += CieXyz(w).y;
        }
        return sum;
    }();
    return integral;
}

void CopyMatrix(float (&target)[16], const glm::mat4& matrix) {
    std::memcpy(target, glm::value_ptr(matrix), sizeof(target));
}

std::uint32_t Groups(std::uint32_t size) {
    return (size + 7) / 8;
}

/// A unit vector perpendicular enough to `direction` to serve as a look-at up vector.
glm::vec3 UpFor(const glm::vec3& direction) {
    return std::abs(direction.z) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                         : glm::vec3(0.0f, 0.0f, 1.0f);
}

/// World depth spanned by the [0, 1] depth of an orthographic view-projection.
float OrthographicDepth(const glm::mat4& viewProjection) {
    const float gradient =
        glm::length(glm::vec3(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2]));
    return gradient > 0.0f ? 1.0f / gradient : 0.0f;
}

/// A compute shader that evaluates `model` once, to check that its module implements `IBrdf`.
std::vector<ShaderModuleSource> BrdfCheckModules(const BrdfModel& model) {
    return {{"brdf", std::string(BrdfSlang())},
            {BrdfModuleName(model), model.slang},
            {"brdf_check", "import brdf;\nimport " + BrdfModuleName(model) +
                               ";\nRWStructuredBuffer<float3> result;\n"
                               "[shader(\"compute\")]\n[numthreads(1, 1, 1)]\n"
                               "void main() {\n    " +
                               model.typeName +
                               " model;\n    BrdfInput input;\n    BrdfParams params;\n"
                               "    result[0] = model.radianceFactor(input, params);\n}\n"}};
}

}  // namespace

std::vector<ShadowCascade> FitLightShadowViews(const Light& light, const glm::vec3& sceneMin,
                                               const glm::vec3& sceneMax,
                                               std::uint32_t resolution) {
    if (!light.shadows) {
        return {};
    }
    const float texels = static_cast<float>(std::max(resolution, 1u));
    const glm::vec3 centre = 0.5f * (sceneMin + sceneMax);
    if (light.type == LightType::Distant) {
        const float radius = std::max(0.5f * glm::length(sceneMax - sceneMin), 1e-3f);
        const glm::vec3 towards = glm::normalize(light.direction);
        ShadowCascade view;
        view.lightViewProjection =
            glm::orthoRH_ZO(-radius, radius, -radius, radius, radius, 3.0f * radius) *
            glm::lookAtRH(centre + 2.0f * radius * towards, centre, UpFor(towards));
        view.texelWorldSize = 2.0f * radius / texels;
        return {view};
    }
    float farPlane = 1e-3f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? sceneMax.x : sceneMin.x, (i & 2) ? sceneMax.y : sceneMin.y,
                               (i & 4) ? sceneMax.z : sceneMin.z);
        farPlane = std::max(farPlane, 1.01f * glm::length(corner - light.position));
    }
    const float nearPlane = std::max(light.radius, 1e-4f * farPlane);
    const auto perspective = [&](const glm::vec3& axis, float fov) {
        ShadowCascade view;
        view.lightViewProjection =
            glm::perspectiveRH_ZO(fov, 1.0f, nearPlane, farPlane) *
            glm::lookAtRH(light.position, light.position + axis, UpFor(axis));
        view.texelWorldSize = 2.0f * std::tan(0.5f * fov) / texels;
        return view;
    };
    if (light.coneAngle <= Light::kMaxSpotConeDegrees) {
        return {perspective(glm::normalize(light.direction),
                            2.0f * glm::radians(std::max(light.coneAngle, 0.5f)))};
    }
    std::vector<ShadowCascade> faces;
    for (const glm::vec3 axis : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                 glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}) {
        faces.push_back(perspective(axis, glm::radians(90.0f)));
    }
    return faces;
}

RasterRenderer::RasterRenderer(const VulkanContext& context, VkFormat presentFormat,
                               BrdfRegistry brdfs)
    : context_(context), presentFormat_(presentFormat), brdfs_(std::move(brdfs)),
      uniforms_(context, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                BufferMemory::Upload),
      shadowUniforms_(context, sizeof(ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      BufferMemory::Upload),
      clampSampler_(context, SamplerDesc{}),
      shadowSampler_(context, SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, true}),
      poisson_(PoissonDisk(16)), shadingLayout_(context, kShadingBindings, 16),
      filterLayout_(context, kFilterBindings), presentLayout_(context, kPresentBindings, 16),
      sigmoidTable_(context, ImageDesc{kTableResolution, kTableResolution, kTableResolution, 1,
                                       VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT}),
      tableSampler_(context, SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, false}),
      shadingUniforms_(context, sizeof(ShadingUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       BufferMemory::Upload),
      sensorUniforms_(context, sizeof(SensorUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      BufferMemory::Upload),
      lightTable_(context, kMaxLights * sizeof(LightGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  BufferMemory::Upload),
      lightViewMatrices_(context, kMaxLightShadowViews * sizeof(glm::mat4),
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload) {
    if (brdfs_.Models().empty()) {
        throw std::invalid_argument("RasterRenderer needs at least one BRDF");
    }
    MaterialSettings defaults;
    if (brdfs_.Find(defaults.brdf) == nullptr) {
        defaults.brdf = brdfs_.Models().front().id;
    }
    materials_.emplace(kDefaultMaterial, std::make_unique<RasterMaterial>(context_));
    SetMaterial(kDefaultMaterial, defaults);
    CreateShadowMaps(1);
    CreateLightShadowMaps(1, 1);

    const std::vector<float>& table = SigmoidTable();
    const std::size_t layerBytes =
        static_cast<std::size_t>(kTableResolution) * kTableResolution * 4 * sizeof(float);
    GpuBuffer staging(context_, table.size() * sizeof(float), 0, BufferMemory::Upload);
    staging.Write(std::as_bytes(std::span(table)));
    context_.Submit([&](VkCommandBuffer commands) {
        sigmoidTable_.Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        std::vector<VkBufferImageCopy> regions;
        for (std::uint32_t layer = 0; layer < kTableResolution; ++layer) {
            VkBufferImageCopy region{};
            region.bufferOffset = layer * layerBytes;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
            region.imageExtent = {kTableResolution, kTableResolution, 1};
            regions.push_back(region);
        }
        vkCmdCopyBufferToImage(commands, staging.Handle(), sigmoidTable_.Handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());
        sigmoidTable_.Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    const std::vector<ShaderModuleSource> filterModules{{"filters", shaders::kFiltersSlang}};
    for (std::size_t i = 0; i < kSensorFilters.size(); ++i) {
        const std::vector<std::uint32_t> spirv =
            compiler_.CompileSpirv(filterModules, kFilterEntries[i]);
        filterPipelines_[i] = std::make_unique<ComputePipeline>(context_, filterLayout_, spirv);
    }
    const std::vector<ShaderModuleSource> presentModules{{"present", shaders::kPresentSlang}};
    const std::vector<std::uint32_t> vertex =
        compiler_.CompileSpirv(presentModules, "presentVertex");
    const std::vector<std::uint32_t> fragment =
        compiler_.CompileSpirv(presentModules, "presentFragment");
    GraphicsPipelineDesc present;
    present.vertexSpirv = vertex;
    present.fragmentSpirv = fragment;
    present.colorFormat = presentFormat_;
    present.depthFormat = VK_FORMAT_UNDEFINED;
    presentPipeline_ = std::make_unique<GraphicsPipeline>(context_, presentLayout_, present);
}

RasterRenderer::~RasterRenderer() {
    vkDeviceWaitIdle(context_.Device());
}

void RasterRenderer::AddBrdf(BrdfModel model) {
    BrdfRegistry candidate = brdfs_;
    candidate.Add(model);
    try {
        compiler_.CompileSpirv(BrdfCheckModules(model), "main");
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument("BRDF " + model.id + " failed to compile: " + error.what());
    }
    brdfs_ = std::move(candidate);
}

std::uint32_t RasterRenderer::AddMaterial(const MaterialSettings& settings) {
    if (brdfs_.Find(settings.brdf) == nullptr) {
        throw std::invalid_argument("Unknown BRDF: " + settings.brdf);
    }
    const std::uint32_t id = nextMaterial_++;
    materials_.emplace(id, std::make_unique<RasterMaterial>(context_));
    SetMaterial(id, settings);
    return id;
}

void RasterRenderer::SetMaterial(std::uint32_t id, const MaterialSettings& settings) {
    const BrdfModel* model = brdfs_.Find(settings.brdf);
    if (model == nullptr) {
        throw std::invalid_argument("Unknown BRDF: " + settings.brdf);
    }
    Material(id).Update(settings, ResolveBrdfParams(*model, settings.brdfParams));
}

RasterMaterial& RasterRenderer::Material(std::uint32_t id) {
    const auto it = materials_.find(id);
    if (it == materials_.end()) {
        throw std::invalid_argument("Unknown material " + std::to_string(id));
    }
    return *it->second;
}

const RasterMaterial& RasterRenderer::Material(std::uint32_t id) const {
    const auto it = materials_.find(id);
    if (it == materials_.end()) {
        throw std::invalid_argument("Unknown material " + std::to_string(id));
    }
    return *it->second;
}

void RasterRenderer::RemoveMaterial(std::uint32_t id) {
    if (id == kDefaultMaterial) {
        throw std::invalid_argument("The default material cannot be removed");
    }
    const auto it = materials_.find(id);
    if (it == materials_.end()) {
        throw std::invalid_argument("Unknown material " + std::to_string(id));
    }
    vkDeviceWaitIdle(context_.Device());
    materials_.erase(it);
}

const RasterMaterial& RasterRenderer::MaterialOrDefault(std::uint32_t id) const {
    const auto it = materials_.find(id);
    return it != materials_.end() ? *it->second : *materials_.at(kDefaultMaterial);
}

void RasterRenderer::CreateShadowMaps(std::uint32_t resolution) {
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

void RasterRenderer::CreateLightShadowMaps(std::uint32_t resolution, std::uint32_t layers) {
    vkDeviceWaitIdle(context_.Device());
    lightShadowMaps_ = std::make_unique<GpuImage>(
        context_,
        ImageDesc{resolution, resolution, layers, 1, VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
                  true});
    context_.Submit([this](VkCommandBuffer commands) {
        lightShadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    lightResolution_ = resolution;
}

void RasterRenderer::WriteLights(const FrameParams& params, const std::optional<Bounds3>& bounds) {
    const std::uint32_t resolution = std::max(params.shadows.lightResolution, 1u);
    lightViews_.clear();
    std::vector<LightGpu> table(params.lights.size());
    for (std::size_t i = 0; i < params.lights.size(); ++i) {
        const Light& light = params.lights[i];
        LightGpu& entry = table[i];
        const glm::vec3 axis = glm::normalize(light.direction);
        const bool distant = light.type == LightType::Distant;
        const glm::vec3 where = distant ? axis : light.position;
        entry.position[0] = where.x;
        entry.position[1] = where.y;
        entry.position[2] = where.z;
        entry.position[3] = static_cast<float>(light.type);
        entry.axis[0] = axis.x;
        entry.axis[1] = axis.y;
        entry.axis[2] = axis.z;
        entry.axis[3] = light.radius;
        const glm::vec3 x = glm::normalize(light.tangent);
        const glm::vec3 y = glm::cross(x, axis);
        const glm::vec2 half =
            light.type == LightType::Disk ? glm::vec2(light.radius) : 0.5f * light.size;
        for (int k = 0; k < 3; ++k) {
            entry.extentX[k] = x[k] * half.x;
            entry.extentY[k] = y[k] * half.y;
        }
        const bool cone = !distant && light.coneAngle < 180.0f;
        const float softness = std::clamp(light.coneSoftness, 0.0f, 1.0f);
        entry.cone[0] = cone ? std::cos(glm::radians(light.coneAngle)) : -2.0f;
        entry.cone[1] = cone ? std::cos(glm::radians(light.coneAngle * (1.0f - softness))) : -1.0f;
        if (bounds) {
            const std::vector<ShadowCascade> views =
                FitLightShadowViews(light, bounds->first, bounds->second, resolution);
            if (!views.empty() && lightViews_.size() + views.size() <= kMaxLightShadowViews) {
                entry.cone[2] = static_cast<float>(lightViews_.size());
                entry.cone[3] = static_cast<float>(views.size());
                entry.shadow[0] = views.front().texelWorldSize;
                lightViews_.insert(lightViews_.end(), views.begin(), views.end());
            }
        }
        std::copy(light.spectrum.begin(), light.spectrum.end(), entry.spectrum);
    }
    if (!table.empty()) {
        lightTable_.Write(std::as_bytes(std::span(table)));
    }
    std::vector<glm::mat4> matrices;
    for (const ShadowCascade& view : lightViews_) {
        matrices.push_back(view.lightViewProjection);
    }
    if (!matrices.empty()) {
        lightViewMatrices_.Write(std::as_bytes(std::span(matrices)));
        const auto layers = static_cast<std::uint32_t>(lightViews_.size());
        if (resolution != lightResolution_ || layers > lightShadowMaps_->Desc().layers) {
            CreateLightShadowMaps(resolution, layers);
        }
    }
}

void RasterRenderer::Update(const FrameParams& params, std::span<RasterPart* const> parts) {
    if (params.lights.size() > kMaxLights) {
        throw std::invalid_argument("A frame has at most " + std::to_string(kMaxLights) +
                                    " lights besides the sun");
    }
    for (const Light& light : params.lights) {
        if (light.spectrum.size() != static_cast<std::size_t>(params.physical.bands.count)) {
            throw std::invalid_argument("Every light's spectrum needs one value per camera band");
        }
        if (glm::length(light.direction) == 0.0f) {
            throw std::invalid_argument("A light's direction must not be zero");
        }
        const bool area = light.type == LightType::Rect || light.type == LightType::Disk;
        if (area && std::abs(glm::dot(glm::normalize(light.tangent),
                                      glm::normalize(light.direction))) > 1e-3f) {
            throw std::invalid_argument("A rect or disk light's tangent must be perpendicular to "
                                        "its direction");
        }
    }
    if (params.dome &&
        params.dome->radiance.size() != static_cast<std::size_t>(params.physical.bands.count)) {
        throw std::invalid_argument("The dome's radiance needs one value per camera band");
    }
    Uniforms uniforms{};
    const glm::mat4 viewProjection = params.camera.ViewProjection();
    std::memcpy(uniforms.viewProjection, glm::value_ptr(viewProjection),
                sizeof(uniforms.viewProjection));
    uniforms.sun[0] = params.sunDirection.x;
    uniforms.sun[1] = params.sunDirection.y;
    uniforms.sun[2] = params.sunDirection.z;
    uniforms.sun[3] = params.ambient;
    uniforms.forward[0] = params.camera.forward.x;
    uniforms.forward[1] = params.camera.forward.y;
    uniforms.forward[2] = params.camera.forward.z;
    uniforms.forward[3] = glm::dot(params.camera.eye, params.camera.forward);
    uniforms.eye[0] = params.camera.eye.x;
    uniforms.eye[1] = params.camera.eye.y;
    uniforms.eye[2] = params.camera.eye.z;
    uniforms.eye[3] = params.exposure;
    uniforms.flags[0] = params.wireframe ? 1u : 0u;
    uniforms_.Write(std::as_bytes(std::span(&uniforms, 1)));

    std::optional<Bounds3> bounds;
    for (const RasterPart* part : parts) {
        if (const std::optional<Bounds3> box = part->Bounds()) {
            bounds = bounds ? Bounds3(glm::min(bounds->first, box->first),
                                      glm::max(bounds->second, box->second))
                            : *box;
        }
    }
    const ShadowSettings& shadows = params.shadows;
    shadowsEnabled_ =
        params.sun && shadows.enabled && bounds.has_value() && params.sunDirection.z > 0.0f;
    cascades_.clear();
    if (shadowsEnabled_) {
        if (shadows.resolution != shadowResolution_) {
            CreateShadowMaps(shadows.resolution);
        }
        const auto [low, high] = *bounds;
        float farthest = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                   (i & 4) ? high.z : low.z);
            farthest =
                std::max(farthest, glm::dot(corner - params.camera.eye, params.camera.forward));
        }
        // A margin keeps surfaces at exactly the farthest depth, such as flat ground under a
        // nadir camera, inside the last cascade.
        const float shadowDistance = std::min(params.camera.farPlane, 1.01f * farthest);
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
        shadowUniforms.depthWorld[c] = OrthographicDepth(cascades_[c].lightViewProjection);
    }
    shadowUniforms.sun[0] = std::tan(0.5f * glm::radians(std::max(params.sunAngle, 0.0f)));
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

    params.physical.Check();
    if (params.physical.sensor.channels.size() > 4) {
        throw std::invalid_argument("A sensor has at most four channels");
    }
    if (!params.sunIrradiance.empty() &&
        params.sunIrradiance.size() != static_cast<std::size_t>(params.physical.bands.count)) {
        throw std::invalid_argument("The sun's irradiance needs one value per camera band");
    }
    filters_ = CheckFilterChain(params.filters.empty() ? DefaultFilterChain() : params.filters);
    bands_ = params.physical.bands;
    extent_ = {params.physical.width, params.physical.height};
    WriteMaterialTable();
    for (const std::size_t model : modelsInUse_) {
        ShadingPipeline(model);
    }
    WriteShading(params);
    WriteSensor(params);
    frameIndex_ += 1;

    WriteLights(params, bounds);
    shadowViews_ = cascades_;
    shadowViews_.insert(shadowViews_.end(), lightViews_.begin(), lightViews_.end());
    for (RasterPart* part : parts) {
        part->Prepare(params, shadowViews_);
    }
    updated_ = true;
}

const ComputePipeline& RasterRenderer::ShadingPipeline(std::size_t model) {
    if (const auto it = shadingPipelines_.find(model); it != shadingPipelines_.end()) {
        return *it->second;
    }
    const BrdfModel& brdf = brdfs_.Models()[model];
    const std::vector<ShaderModuleSource> modules{
        {"brdf", std::string(BrdfSlang())},
        {BrdfModuleName(brdf), brdf.slang},
        {"shading", "import " + BrdfModuleName(brdf) + ";\ntypealias ActiveBrdf = " +
                        brdf.typeName + ";\n" + shaders::kShadingSlang}};
    std::vector<std::uint32_t> spirv;
    try {
        spirv = compiler_.CompileSpirv(modules, "shadeMain");
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument("BRDF " + brdf.id +
                                    " failed to compile for shading: " + error.what());
    }
    auto pipeline = std::make_unique<ComputePipeline>(context_, shadingLayout_, spirv);
    return *shadingPipelines_.emplace(model, std::move(pipeline)).first->second;
}

void RasterRenderer::WriteMaterialTable() {
    const std::uint32_t count = materials_.rbegin()->first + 1;
    std::vector<MaterialGpu> table(count, MaterialGpu{});
    std::set<std::size_t> models;
    const std::span<const BrdfModel> registered = brdfs_.Models();
    for (const auto& [id, material] : materials_) {
        MaterialGpu& entry = table[id];
        const auto model =
            std::find_if(registered.begin(), registered.end(),
                         [&](const BrdfModel& m) { return m.id == material->Settings().brdf; });
        const auto index = static_cast<std::size_t>(model - registered.begin());
        entry.info[0] = static_cast<std::uint32_t>(index);
        models.insert(index);
        const std::vector<float>& params = material->Params();
        std::copy_n(params.begin(), std::min<std::size_t>(params.size(), BrdfRegistry::kMaxParams),
                    entry.params);
        std::vector<float> shape = material->Reflectance().Bin(bands_);
        const float luminance = material->Luminance();
        for (float& value : shape) {
            value = luminance > kBlackLuminance ? value / luminance : 1.0f;
        }
        std::copy(shape.begin(), shape.end(), entry.reflectance);
    }
    modelsInUse_.assign(models.begin(), models.end());
    const VkDeviceSize bytes = table.size() * sizeof(MaterialGpu);
    if (materialTable_ == nullptr || materialTable_->Size() < bytes) {
        vkDeviceWaitIdle(context_.Device());
        materialTable_ = std::make_unique<GpuBuffer>(
            context_, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload);
    }
    materialTable_->Write(std::as_bytes(std::span(table)));
}

void RasterRenderer::WriteShading(const FrameParams& params) {
    ShadingUniforms shading{};
    CopyMatrix(shading.invViewProj, glm::inverse(params.camera.ViewProjection()));
    shading.eye[0] = params.camera.eye.x;
    shading.eye[1] = params.camera.eye.y;
    shading.eye[2] = params.camera.eye.z;
    shading.sun[0] = params.sunDirection.x;
    shading.sun[1] = params.sunDirection.y;
    shading.sun[2] = params.sunDirection.z;
    shading.sun[3] = params.ambient;
    shading.range[0] = static_cast<float>(bands_.minNm);
    shading.range[1] = static_cast<float>(bands_.Width());
    shading.range[2] = static_cast<float>(bands_.count);
    shading.size[0] = extent_.width;
    shading.size[1] = extent_.height;
    shading.size[2] = kTableResolution;
    shading.size[3] = static_cast<std::uint32_t>(params.lights.size());
    for (int i = 0; i < bands_.count && params.sun; ++i) {
        shading.irradiance[i] =
            params.sunIrradiance.empty()
                ? static_cast<float>(M_PI / (kLuminousEfficacy * LuminanceIntegral()))
                : params.sunIrradiance[static_cast<std::size_t>(i)];
    }
    if (params.dome) {
        for (std::size_t k = 0; k < params.dome->sh.size(); ++k) {
            shading.domeSh[k][0] = params.dome->sh[k].r;
            shading.domeSh[k][1] = params.dome->sh[k].g;
            shading.domeSh[k][2] = params.dome->sh[k].b;
        }
        std::copy(params.dome->radiance.begin(), params.dome->radiance.end(), shading.dome);
    }
    shading.lightShadow[0] = params.shadows.softness;
    shading.lightShadow[1] = params.shadows.normalOffset;
    shading.lightShadow[2] = static_cast<float>(lightResolution_);
    for (std::size_t i = 0; i < poisson_.size(); ++i) {
        shading.poisson[2 * i] = poisson_[i].x;
        shading.poisson[2 * i + 1] = poisson_[i].y;
    }
    shadingUniforms_.Write(std::as_bytes(std::span(&shading, 1)));
}

void RasterRenderer::WriteSensor(const FrameParams& params) {
    const PhysicalCamera& camera = params.physical;
    const glm::mat4& projection = params.camera.projection;
    const double rasterFocal =
        static_cast<double>(projection[1][1]) * static_cast<double>(extent_.height) / 2.0;
    SensorUniforms sensor{};
    CopyMatrix(sensor.invViewProj, glm::inverse(params.camera.ViewProjection()));
    sensor.eye[0] = params.camera.eye.x;
    sensor.eye[1] = params.camera.eye.y;
    sensor.eye[2] = params.camera.eye.z;
    sensor.forward[0] = params.camera.forward.x;
    sensor.forward[1] = params.camera.forward.y;
    sensor.forward[2] = params.camera.forward.z;
    sensor.intrinsics[0] = static_cast<float>(rasterFocal * params.overscan);
    sensor.intrinsics[1] =
        static_cast<float>(extent_.width / 2.0 * (1.0 - static_cast<double>(projection[2][0])));
    sensor.intrinsics[2] =
        static_cast<float>(extent_.height / 2.0 * (1.0 + static_cast<double>(projection[2][1])));
    sensor.intrinsics[3] = static_cast<float>(params.overscan);
    sensor.size[0] = extent_.width;
    sensor.size[1] = extent_.height;
    sensor.size[2] = static_cast<std::uint32_t>(bands_.count);
    sensor.size[3] = static_cast<std::uint32_t>(camera.sensor.channels.size());
    const LensModel& lens = camera.lens;
    sensor.distortion[0] = static_cast<float>(lens.k1);
    sensor.distortion[1] = static_cast<float>(lens.k2);
    sensor.distortion[2] = static_cast<float>(lens.k3);
    sensor.distortion[3] = static_cast<float>(lens.p1);
    sensor.lens[0] = static_cast<float>(lens.p2);
    sensor.lens[1] = lens.vignetting ? 1.0f : 0.0f;
    sensor.lens[2] =
        static_cast<float>(camera.LinearExposureScale() * static_cast<double>(params.exposure));
    sensor.lens[3] = static_cast<float>(camera.exposureTime);
    const SensorModel& model = camera.sensor;
    sensor.sensor[0] = static_cast<float>(model.fullWellElectrons);
    sensor.sensor[1] = static_cast<float>(model.readNoiseElectrons);
    sensor.sensor[2] = static_cast<float>(model.darkCurrentElectrons);
    sensor.sensor[3] = static_cast<float>(model.prnu);
    sensor.adc[0] = static_cast<float>(model.dsnuElectrons);
    sensor.adc[1] = static_cast<float>(model.ElectronsPerDn(camera.iso));
    sensor.adc[2] = static_cast<float>(model.blackLevelDn);
    sensor.adc[3] = static_cast<float>(std::exp2(model.adcBits) - 1.0);
    sensor.noise[0] = model.noise ? 1u : 0u;
    sensor.noise[1] = model.seed;
    sensor.noise[2] = frameIndex_;
    sensor.clear[0] = params.clearColor.r;
    sensor.clear[1] = params.clearColor.g;
    sensor.clear[2] = params.clearColor.b;
    const std::vector<glm::dvec3> weights = CieWeights(bands_);
    for (int i = 0; i < bands_.count; ++i) {
        const glm::dvec3 rgb =
            EqualEnergyXyzToLinearSrgb(weights[static_cast<std::size_t>(i)] * kLuminousEfficacy);
        sensor.colour[i][0] = static_cast<float>(rgb.r);
        sensor.colour[i][1] = static_cast<float>(rgb.g);
        sensor.colour[i][2] = static_cast<float>(rgb.b);
    }
    for (std::size_t c = 0; c < model.channels.size(); ++c) {
        const std::vector<float> factors = camera.ElectronsPerRadiance(c);
        for (int i = 0; i < bands_.count; ++i) {
            sensor.qe[i][c] = factors[static_cast<std::size_t>(i)];
        }
    }
    sensorUniforms_.Write(std::as_bytes(std::span(&sensor, 1)));
}

RasterFrame RasterRenderer::Frame() const {
    return {DescriptorResource::Of(uniforms_), DescriptorResource::Of(shadowUniforms_),
            DescriptorResource::Of(shadowMaps_->View()), DescriptorResource::Of(shadowSampler_),
            DescriptorResource::Of(clampSampler_)};
}

void RasterRenderer::Record(VkCommandBuffer commands, RenderTargets& targets,
                            std::span<const RasterPart* const> parts,
                            const RenderingTarget* present) const {
    if (!updated_) {
        if (present != nullptr) {
            BeginRendering(commands, *present);
            vkCmdEndRendering(commands);
        }
        return;
    }
    if (targets.Extent().width != extent_.width || targets.Extent().height != extent_.height ||
        targets.Bands() != bands_.count) {
        throw std::invalid_argument("Render targets need the camera's extent and band count");
    }
    const RasterFrame frame = Frame();
    if (shadowsEnabled_) {
        shadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        for (std::size_t c = 0; c < cascades_.size(); ++c) {
            RenderingTarget shadow;
            shadow.depth = shadowMaps_->LayerView(static_cast<std::uint32_t>(c));
            shadow.extent = {shadowResolution_, shadowResolution_};
            BeginRendering(commands, shadow);
            for (const RasterPart* part : parts) {
                part->RecordShadow(commands, frame, c, cascades_[c].lightViewProjection);
            }
            vkCmdEndRendering(commands);
        }
        shadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (!lightViews_.empty()) {
        lightShadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        for (std::size_t v = 0; v < lightViews_.size(); ++v) {
            RenderingTarget shadow;
            shadow.depth = lightShadowMaps_->LayerView(static_cast<std::uint32_t>(v));
            shadow.extent = {lightResolution_, lightResolution_};
            BeginRendering(commands, shadow);
            for (const RasterPart* part : parts) {
                part->RecordShadow(commands, frame, cascades_.size() + v,
                                   lightViews_[v].lightViewProjection);
            }
            vkCmdEndRendering(commands);
        }
        lightShadowMaps_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    BeginRendering(commands, targets.PrepareGBuffer(commands));
    for (const RasterPart* part : parts) {
        part->RecordColor(commands, frame);
    }
    vkCmdEndRendering(commands);
    ShadeAndFilter(commands, targets);
    if (present != nullptr) {
        Present(commands, targets, *present);
    }
}

void RasterRenderer::ShadeAndFilter(VkCommandBuffer commands, RenderTargets& targets) const {
    targets.PrepareCompute(commands);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                      static_cast<std::uint32_t>(targets.Bands())};
    vkCmdClearColorImage(commands, targets.RasterRadiance().Handle(), VK_IMAGE_LAYOUT_GENERAL,
                         &zero, 1, &all);
    ShaderBarrier(commands);

    const auto sampled = [](const GpuImage& image) {
        return DescriptorResource::Of(image.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    const auto general = [](const GpuImage& image) {
        return DescriptorResource::Of(image.View(), VK_IMAGE_LAYOUT_GENERAL);
    };
    const std::vector<DescriptorResource> shading{DescriptorResource::Of(shadingUniforms_),
                                                  sampled(targets.GBuffer(0)),
                                                  sampled(targets.GBuffer(1)),
                                                  sampled(targets.GBuffer(2)),
                                                  sampled(targets.Depth()),
                                                  DescriptorResource::Of(*materialTable_),
                                                  sampled(sigmoidTable_),
                                                  DescriptorResource::Of(tableSampler_),
                                                  general(targets.RasterRadiance()),
                                                  DescriptorResource::Of(lightTable_),
                                                  sampled(*lightShadowMaps_),
                                                  DescriptorResource::Of(shadowSampler_),
                                                  DescriptorResource::Of(lightViewMatrices_)};
    const std::uint32_t groupsX = Groups(extent_.width);
    const std::uint32_t groupsY = Groups(extent_.height);
    for (const std::size_t model : modelsInUse_) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          shadingPipelines_.at(model)->Handle());
        shadingLayout_.Push(commands, VK_PIPELINE_BIND_POINT_COMPUTE, shading);
        const std::uint32_t constants[4] = {static_cast<std::uint32_t>(model), 0, 0, 0};
        shadingLayout_.PushConstants(commands, constants, sizeof(constants));
        vkCmdDispatch(commands, groupsX, groupsY, 1);
    }
    ShaderBarrier(commands);

    std::vector<DescriptorResource> filters{
        DescriptorResource::Of(sensorUniforms_), general(targets.RasterRadiance()),
        sampled(targets.GBuffer(0)), sampled(targets.GBuffer(2)), sampled(targets.Depth())};
    for (const std::string_view name : RenderTargets::VarNames()) {
        filters.push_back(general(*targets.Var(name)));
    }
    for (const SensorFilter filter : filters_) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          filterPipelines_[static_cast<std::size_t>(filter)]->Handle());
        filterLayout_.Push(commands, VK_PIPELINE_BIND_POINT_COMPUTE, filters);
        vkCmdDispatch(commands, groupsX, groupsY, 1);
        ShaderBarrier(commands);
    }
}

void RasterRenderer::Present(VkCommandBuffer commands, RenderTargets& targets,
                             const RenderingTarget& present) const {
    GpuImage& ldr = *targets.Var(render_vars::kLdrColor);
    ldr.Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    RenderingTarget target = present;
    target.depth = VK_NULL_HANDLE;
    BeginRendering(commands, target);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline_->Handle());
    const DescriptorResource image =
        DescriptorResource::Of(ldr.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    presentLayout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, std::span(&image, 1));
    const bool srgb =
        presentFormat_ == VK_FORMAT_R8G8B8A8_SRGB || presentFormat_ == VK_FORMAT_B8G8R8A8_SRGB;
    const std::uint32_t flags[4] = {srgb ? 1u : 0u, 0, 0, 0};
    presentLayout_.PushConstants(commands, flags, sizeof(flags));
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    ldr.Transition(commands, VK_IMAGE_LAYOUT_GENERAL);
}

std::uint64_t RasterRenderer::GpuBytes() const {
    std::uint64_t bytes = uniforms_.Size() + shadowUniforms_.Size() + shadowMaps_->Bytes() +
                          lightShadowMaps_->Bytes() + lightTable_.Size() +
                          lightViewMatrices_.Size() + sigmoidTable_.Bytes() +
                          shadingUniforms_.Size() + sensorUniforms_.Size() +
                          (materialTable_ != nullptr ? materialTable_->Size() : 0);
    for (const auto& [id, material] : materials_) {
        bytes += material->GpuBytes();
    }
    return bytes;
}

}  // namespace ernie
