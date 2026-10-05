#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/material/brdf.h"
#include "ernie/raster/raster_material.h"
#include "ernie/raster/raster_part.h"
#include "ernie/raster/render_targets.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ernie {

/// The shadow views of `light` over a scene within `sceneMin` to `sceneMax`, for square maps of
/// `resolution` texels:
/// - a distant light: one orthographic view along the light enclosing the scene's bounding sphere;
///   `texelWorldSize` is the world size of a texel;
/// - a sphere light whose cone is at most `Light::kMaxSpotConeDegrees`: one perspective view along
///   its axis covering the cone;
/// - any other sphere light: six 90-degree perspective views, the faces of a cube in the order
///   +x, -x, +y, -y, +z, -z.
/// Perspective views reach from the light (its radius, at least 1/10000 of the far plane) to the
/// scene's farthest corner, and their `texelWorldSize` is the size of a texel one unit away. None
/// for a light without shadows.
std::vector<ShadowCascade> FitLightShadowViews(const Light& light, const glm::vec3& sceneMin,
                                               const glm::vec3& sceneMax, std::uint32_t resolution);

/// Ernie's rasterizer: owns what every part of a frame shares and draws the parts, spectrally.
///
/// Shared are the frame uniforms (camera, sun, ambient), the lights, the shadow maps, a table of
/// materials, the reflectance models and the shader compiler. Parts (`TerrainPart`, `MeshPart`,
/// ...) are created against a renderer and passed to `Update` and `Record` each frame: the
/// renderer fits the shadow cascades around all their bounds, so every part casts shadows on every
/// other.
///
/// A frame is deferred and spectral. The parts rasterize a G-buffer (shading normals, shadow
/// visibility, albedo scale and modulation, material and prim ids, depth); a compute pass per
/// reflectance model in use turns it into spectral radiance in the camera's bands, lit by the
/// sun's spectral irradiance and the other lights (`Light`); then the camera's sensor filters
/// (`SensorFilter`) turn that into the render vars, and a present pass can draw LdrColor into a
/// colour attachment.
///
/// The sun's shadows use cascaded shadow maps (`FitCascades`) filtered with rotated Poisson-disk
/// PCF, with slope-scaled depth bias and normal offset against acne, and penumbrae that widen with
/// the distance to the occluder for the sun's angular size (percentage-closer soft shadows). Other
/// lights' shadow maps (`FitLightShadowViews`) are sampled with the same filter in the shading
/// pass.
///
/// References:
/// - L. Williams, "Casting Curved Shadows on Curved Surfaces", SIGGRAPH '78, 270-274, 1978.
///   https://doi.org/10.1145/965139.807402
/// - W. T. Reeves, D. H. Salesin and R. L. Cook, "Rendering Antialiased Shadows with Depth Maps",
///   SIGGRAPH '87, 283-291, 1987. https://doi.org/10.1145/37402.37435
/// - D. Holbert, "Saying 'Goodbye' to Shadow Acne", GDC 2011 poster (normal offset shadows).
/// - J. Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
///   Advances in Real-Time Rendering course (interleaved gradient noise).
/// - R. Fernando, "Percentage-Closer Soft Shadows", SIGGRAPH 2005 Sketches, 2005.
///   https://doi.org/10.1145/1187112.1187153
/// - T. Saito and T. Takahashi, "Comprehensible Rendering of 3-D Shapes", SIGGRAPH '90, 197-206,
///   1990. https://doi.org/10.1145/97880.97901 (G-buffers and deferred shading).
/// - W. Jakob and J. Hanika, "A Low-Dimensional Function Space for Efficient Spectral
///   Upsampling", Computer Graphics Forum 38(2):147-155, 2019 (RGB modulation to spectra).
class RasterRenderer {
  public:
    static constexpr int kMaxCascades = 4;

    /// Lights besides the sun per frame, and shadow map views among them; lights past the views
    /// draw without shadows.
    static constexpr std::size_t kMaxLights = 16;
    static constexpr std::size_t kMaxLightShadowViews = 24;

    /// Material 0, which always exists and parts use by default: Lambert with albedo 1.
    static constexpr std::uint32_t kDefaultMaterial = 0;

    /// Shades with the models in `brdfs`, and presents LdrColor into colour attachments of
    /// `presentFormat`.
    /// @throws std::runtime_error if the shared resources cannot be created.
    explicit RasterRenderer(const VulkanContext& context,
                            VkFormat presentFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());
    ~RasterRenderer();
    RasterRenderer(const RasterRenderer&) = delete;
    RasterRenderer& operator=(const RasterRenderer&) = delete;

    const VulkanContext& Context() const {
        return context_;
    }

    VkFormat PresentFormat() const {
        return presentFormat_;
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

    /// Writes the frame, shading and sensor uniforms and the material table, fits the shadow
    /// cascades around the parts' bounds and prepares every part. Call once per frame before
    /// `Record`, only while no submitted frame is still reading the renderer's or the parts'
    /// buffers.
    /// @throws std::invalid_argument for an invalid camera (`Camera::Check`) or filter
    /// chain (`CheckFilterChain`), a sun or light spectrum with the wrong number of bands, more
    /// than `kMaxLights` lights, or a model whose shading pass fails to compile.
    void Update(const FrameParams& params, std::span<RasterPart* const> parts);

    /// Records a frame of `parts` into `targets`: the shadow passes, the G-buffer, spectral shading
    /// and the sensor filters of the last `Update`. With `present`, then draws LdrColor into
    /// `present.color` (in the colour attachment layout, of `PresentFormat`). Before any `Update`
    /// only clears `present`.
    /// @throws std::invalid_argument if `targets` do not have the camera's extent and bands.
    void Record(VkCommandBuffer commands, RenderTargets& targets,
                std::span<const RasterPart* const> parts,
                const RenderingTarget* present = nullptr) const;

    /// The spectral bands and filter chain of the last `Update`.
    const SpectralBands& Bands() const {
        return bands_;
    }

    const std::vector<SensorFilter>& Filters() const {
        return filters_;
    }

    /// The image extent of the last `Update`.
    VkExtent2D Extent() const {
        return extent_;
    }

    /// Frame resources for parts: uniforms, shadows and samplers (bindings 0 to 4).
    RasterFrame Frame() const;

    /// Cascades fitted by the last `Update`; empty with shadows off.
    const std::vector<ShadowCascade>& Cascades() const {
        return cascades_;
    }

    /// Shadow views of the other lights fitted by the last `Update`, in light order.
    const std::vector<ShadowCascade>& LightShadowViews() const {
        return lightViews_;
    }

    /// GPU memory held for the frame, shadow maps and materials, in bytes.
    std::uint64_t GpuBytes() const;

  private:
    void CreateShadowMaps(std::uint32_t resolution);
    void CreateLightShadowMaps(std::uint32_t resolution, std::uint32_t layers);
    void WriteLights(const FrameParams& params, const std::optional<Bounds3>& bounds);
    const ComputePipeline& ShadingPipeline(std::size_t model);
    void WriteMaterialTable();
    void WriteShading(const FrameParams& params);
    void WriteSensor(const FrameParams& params);
    void ShadeAndFilter(VkCommandBuffer commands, RenderTargets& targets) const;
    void Present(VkCommandBuffer commands, RenderTargets& targets,
                 const RenderingTarget& present) const;

    const VulkanContext& context_;
    VkFormat presentFormat_;
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
    std::vector<ShadowCascade> lightViews_;
    std::vector<ShadowCascade> shadowViews_;
    std::unique_ptr<GpuImage> lightShadowMaps_;
    std::uint32_t lightResolution_ = 0;
    std::vector<glm::vec2> poisson_;
    bool shadowsEnabled_ = false;
    bool updated_ = false;

    PipelineLayout shadingLayout_;
    std::map<std::size_t, std::unique_ptr<ComputePipeline>> shadingPipelines_;
    PipelineLayout filterLayout_;
    std::array<std::unique_ptr<ComputePipeline>, kSensorFilters.size()> filterPipelines_;
    PipelineLayout presentLayout_;
    std::unique_ptr<GraphicsPipeline> presentPipeline_;
    GpuImage sigmoidTable_;
    Sampler tableSampler_;
    GpuBuffer shadingUniforms_;
    GpuBuffer sensorUniforms_;
    GpuBuffer lightTable_;
    GpuBuffer lightViewMatrices_;
    std::unique_ptr<GpuBuffer> materialTable_;
    std::vector<std::size_t> modelsInUse_;
    SpectralBands bands_;
    std::vector<SensorFilter> filters_;
    VkExtent2D extent_{0, 0};
};

}  // namespace ernie
