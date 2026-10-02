#include "ernie/raster/raster_renderer.h"

#include "ernie/raster/poisson_disk.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
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
    float params[4];
    std::uint32_t debug[4];
    float poisson[32];
};
static_assert(sizeof(ShadowUniforms) == 448, "ShadowUniforms must match frame.slang");

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

RasterRenderer::RasterRenderer(const VulkanContext& context, VkFormat colorFormat,
                               VkFormat depthFormat, BrdfRegistry brdfs)
    : context_(context), colorFormat_(colorFormat), depthFormat_(depthFormat),
      brdfs_(std::move(brdfs)), uniforms_(context, sizeof(Uniforms),
                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemory::Upload),
      shadowUniforms_(context, sizeof(ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      BufferMemory::Upload),
      clampSampler_(context, SamplerDesc{}),
      shadowSampler_(context, SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, true}),
      poisson_(PoissonDisk(16)) {
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

void RasterRenderer::Update(const FrameParams& params, std::span<RasterPart* const> parts) {
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
    shadowsEnabled_ = shadows.enabled && bounds.has_value() && params.sunDirection.z > 0.0f;
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

    for (RasterPart* part : parts) {
        part->Prepare(params, cascades_);
    }
    updated_ = true;
}

RasterFrame RasterRenderer::Frame() const {
    return {DescriptorResource::Of(uniforms_), DescriptorResource::Of(shadowUniforms_),
            DescriptorResource::Of(shadowMaps_->View()), DescriptorResource::Of(shadowSampler_),
            DescriptorResource::Of(clampSampler_)};
}

void RasterRenderer::Record(VkCommandBuffer commands, const RenderingTarget& target,
                            std::span<const RasterPart* const> parts) const {
    const RasterFrame frame = Frame();
    if (updated_ && shadowsEnabled_) {
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
    BeginRendering(commands, target);
    if (updated_) {
        for (const RasterPart* part : parts) {
            part->RecordColor(commands, frame);
        }
    }
    vkCmdEndRendering(commands);
}

std::uint64_t RasterRenderer::GpuBytes() const {
    std::uint64_t bytes = uniforms_.Size() + shadowUniforms_.Size() + shadowMaps_->Bytes();
    for (const auto& [id, material] : materials_) {
        bytes += material->GpuBytes();
    }
    return bytes;
}

}  // namespace ernie
