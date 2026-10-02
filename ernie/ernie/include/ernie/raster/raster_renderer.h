#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/material/brdf.h"
#include "ernie/raster/raster_material.h"
#include "ernie/raster/raster_part.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace ernie {

/// Ernie's rasterizer: owns what every part of a frame shares and draws the parts.
///
/// Shared are the frame uniforms (camera, sun, ambient, exposure), the cascaded shadow maps, a
/// table of materials, the reflectance models and the shader compiler. Parts (`TerrainPart`,
/// `MeshPart`, ...) are created against a renderer and passed to `Update` and `Record` each frame:
/// the renderer fits the shadow cascades around all their bounds, so every part casts shadows on
/// every other, and lights them all with the same sun, shadows and models.
///
/// Shadows use cascaded shadow maps (`FitCascades`) filtered with rotated Poisson-disk PCF, with
/// slope-scaled depth bias and normal offset against acne.
///
/// References:
/// - L. Williams, "Casting Curved Shadows on Curved Surfaces", SIGGRAPH '78, 270-274, 1978.
///   https://doi.org/10.1145/965139.807402
/// - W. T. Reeves, D. H. Salesin and R. L. Cook, "Rendering Antialiased Shadows with Depth Maps",
///   SIGGRAPH '87, 283-291, 1987. https://doi.org/10.1145/37402.37435
/// - D. Holbert, "Saying 'Goodbye' to Shadow Acne", GDC 2011 poster (normal offset shadows).
/// - J. Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
///   Advances in Real-Time Rendering course (interleaved gradient noise).
class RasterRenderer {
  public:
    static constexpr int kMaxCascades = 4;

    /// Material 0, which always exists and parts use by default: Lambert with albedo 1.
    static constexpr std::uint32_t kDefaultMaterial = 0;

    /// Draws into colour and depth attachments of the given formats with the models in `brdfs`.
    /// @throws std::runtime_error if the shared resources cannot be created.
    RasterRenderer(const VulkanContext& context, VkFormat colorFormat,
                   VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
                   BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());
    ~RasterRenderer();
    RasterRenderer(const RasterRenderer&) = delete;
    RasterRenderer& operator=(const RasterRenderer&) = delete;

    const VulkanContext& Context() const {
        return context_;
    }

    VkFormat ColorFormat() const {
        return colorFormat_;
    }

    VkFormat DepthFormat() const {
        return depthFormat_;
    }

    const ShaderCompiler& Compiler() const {
        return compiler_;
    }

    const BrdfRegistry& Brdfs() const {
        return brdfs_;
    }

    /// Registers `model` after checking that its Slang module compiles against `IBrdf`.
    /// @throws std::invalid_argument if the registry rejects the model (`BrdfRegistry::Add`) or
    /// its module fails to compile; the message includes the compiler's diagnostics.
    void AddBrdf(BrdfModel model);

    /// Adds a material with `settings` and returns its id.
    /// @throws std::invalid_argument as `SetMaterial`.
    std::uint32_t AddMaterial(const MaterialSettings& settings = {});

    /// Sets a material's settings, resolving its BRDF parameters.
    /// @throws std::invalid_argument for an unknown material or BRDF.
    void SetMaterial(std::uint32_t id, const MaterialSettings& settings);

    /// The material with `id`, for its albedo map and detail textures.
    /// @throws std::invalid_argument for an unknown material.
    RasterMaterial& Material(std::uint32_t id);
    const RasterMaterial& Material(std::uint32_t id) const;

    bool HasMaterial(std::uint32_t id) const {
        return materials_.contains(id);
    }

    /// Removes a material; parts drawing with it fall back to the default material.
    /// @throws std::invalid_argument for the default material or an unknown one.
    void RemoveMaterial(std::uint32_t id);

    /// The material parts should use for `id`: that material, or the default one if it is gone.
    const RasterMaterial& MaterialOrDefault(std::uint32_t id) const;

    /// Writes the frame uniforms, fits the shadow cascades around the parts' bounds and prepares
    /// every part. Call once per frame before `Record`, only while no submitted frame is still
    /// reading the renderer's or the parts' buffers.
    void Update(const FrameParams& params, std::span<RasterPart* const> parts);

    /// Records the shadow passes and the main pass of `parts` into `commands`. The main pass
    /// renders into `target`, whose attachments must be in colour and depth attachment layouts,
    /// clearing them first unless `target.loadColor` is set. Draws only the clear before the first
    /// `Update`.
    void Record(VkCommandBuffer commands, const RenderingTarget& target,
                std::span<const RasterPart* const> parts) const;

    /// Frame resources for parts: uniforms, shadows and samplers (bindings 0 to 4).
    RasterFrame Frame() const;

    /// Cascades fitted by the last `Update`; empty with shadows off.
    const std::vector<ShadowCascade>& Cascades() const {
        return cascades_;
    }

    /// GPU memory held for the frame, shadow maps and materials, in bytes.
    std::uint64_t GpuBytes() const;

  private:
    void CreateShadowMaps(std::uint32_t resolution);

    const VulkanContext& context_;
    VkFormat colorFormat_;
    VkFormat depthFormat_;
    ShaderCompiler compiler_;
    BrdfRegistry brdfs_;
    std::map<std::uint32_t, std::unique_ptr<RasterMaterial>> materials_;
    std::uint32_t nextMaterial_ = 1;

    GpuBuffer uniforms_;
    GpuBuffer shadowUniforms_;
    Sampler clampSampler_;
    Sampler shadowSampler_;
    std::unique_ptr<GpuImage> shadowMaps_;
    std::uint32_t shadowResolution_ = 0;
    std::vector<ShadowCascade> cascades_;
    std::vector<glm::vec2> poisson_;
    bool shadowsEnabled_ = false;
    bool updated_ = false;
};

}  // namespace ernie
