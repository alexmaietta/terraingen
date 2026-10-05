#include "ernie/raster/ground_part.h"

#include "embedded_shaders.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ernie {

namespace {

struct GroundUniforms {
    float invViewProj[16];
    float plane[4];
    float colour0[4];
    float colour1[4];
    float viewport[4];
    std::uint32_t ids[4];
};
static_assert(sizeof(GroundUniforms) == 144, "GroundUniforms must match ground.slang");

/// frame.slang's per-draw push constants, which the pass declares but leaves unused.
constexpr std::uint32_t kDrawConstantsSize = 80;

constexpr VkShaderStageFlags kGraphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

/// Frame (0 to 4), material (5 to 9) and ground (10) bindings; see ground.slang.
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
};

}  // namespace

GroundPart::GroundPart(RasterRenderer& renderer)
    : renderer_(renderer), layout_(renderer.Context(), kBindings, kDrawConstantsSize),
      uniforms_(renderer.Context(), sizeof(GroundUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                BufferMemory::Upload) {
    const std::vector<ShaderModuleSource> modules{{"brdf", std::string(BrdfSlang())},
                                                  {"frame", shaders::kFrameSlang},
                                                  {"surface", shaders::kSurfaceSlang},
                                                  {"ground", shaders::kGroundSlang}};
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
    pipeline_ = std::make_unique<GraphicsPipeline>(renderer_.Context(), layout_, desc);
}

GroundPart::~GroundPart() {
    vkDeviceWaitIdle(renderer_.Context().Device());
}

void GroundPart::SetParams(const GroundParams& params) {
    if (!(params.checkerSize > 0.0f) || !std::isfinite(params.checkerSize)) {
        throw std::invalid_argument("The ground's checker size must be positive and finite");
    }
    params_ = params;
}

void GroundPart::Prepare(const FrameParams& params, std::span<const ShadowCascade>) {
    prepared_ = false;
    if (!enabled_) {
        return;
    }
    GroundUniforms uniforms{};
    const glm::mat4 inverse = glm::inverse(params.View().ViewProjection());
    std::memcpy(uniforms.invViewProj, glm::value_ptr(inverse), sizeof(uniforms.invViewProj));
    uniforms.plane[0] = params_.height;
    uniforms.plane[1] = params_.checkerSize;
    uniforms.plane[2] = params_.checker ? 1.0f : 0.0f;
    for (int k = 0; k < 3; ++k) {
        uniforms.colour0[k] = params_.checkerColors[0][k];
        uniforms.colour1[k] = params_.checkerColors[1][k];
    }
    uniforms.viewport[0] = static_cast<float>(params.camera.width);
    uniforms.viewport[1] = static_cast<float>(params.camera.height);
    uniforms.ids[0] = renderer_.HasMaterial(params_.material) ? params_.material
                                                              : RasterRenderer::kDefaultMaterial;
    uniforms.ids[1] = params_.primId;
    uniforms_.Write(std::as_bytes(std::span(&uniforms, 1)));
    prepared_ = true;
}

void GroundPart::RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const {
    if (!prepared_) {
        return;
    }
    std::vector<DescriptorResource> resources = {frame.uniforms, frame.shadows, frame.shadowMap,
                                                 frame.shadowSampler, frame.clampSampler};
    renderer_.MaterialOrDefault(params_.material).AppendResources(resources);
    resources.push_back(DescriptorResource::Of(uniforms_));
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->Handle());
    layout_.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, resources);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

}  // namespace ernie
