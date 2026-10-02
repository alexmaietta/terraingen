#pragma once

#include "render/gpu_context.h"
#include "render/orbit_camera.h"
#include "render/shadow_cascades.h"
#include "terraingen/core/raster.h"

#include <glm/glm.hpp>
#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace terraingen::render {

/// How terrain fragments are coloured.
enum class TerrainColorMode : std::uint32_t {
    Hillshade = 0,       ///< Grey Lambert shading.
    Colormap = 1,        ///< Height colormap without shading.
    ShadedColormap = 2,  ///< Height colormap modulated by Lambert shading.
};

/// Cascaded shadow map options.
struct ShadowSettings {
    bool enabled = true;
    int cascades = 3;                 ///< Number of cascades, 1 to 4.
    std::uint32_t resolution = 2048;  ///< Texels along each side of every cascade's map.
    float softness = 1.5f;            ///< Poisson filter radius, in shadow map texels.
    float normalOffset = 1.5f;        ///< Receiver offset along the normal, in texels.
    float splitLambda = 0.8f;         ///< Logarithmic weight of the practical split scheme.
    bool showCascades = false;        ///< Tint fragments by cascade, for tuning.
};

/// Per-frame drawing parameters.
struct TerrainDrawParams {
    CameraView camera;
    glm::vec3 sunDirection{0.0f, 0.0f, 1.0f};  ///< Unit vector pointing towards the sun.
    float ambient = 0.15f;                     ///< Fraction of light reaching unlit slopes.
    float exaggeration = 1.0f;                 ///< Vertical exaggeration of heights.
    TerrainColorMode colorMode = TerrainColorMode::ShadedColormap;
    bool wireframe = false;
    ShadowSettings shadows;
};

/// Draws a north-up heightfield raster as a lit, shadowed triangle mesh.
///
/// Heights live in an `R32Float` texture that the vertex shader reads; the mesh has no vertex or
/// index buffers, since each vertex's grid cell is derived from its vertex index. Normals come from
/// central differences in the shader. The world frame is the raster's: x east, y north, z up.
///
/// Shadows use cascaded shadow maps (`FitCascades`) filtered with rotated Poisson-disk PCF, with
/// slope-scaled depth bias and normal offset against acne.
///
/// References:
/// - L. Williams, "Casting Curved Shadows on Curved Surfaces", SIGGRAPH '78, 270–274, 1978.
///   https://doi.org/10.1145/965139.807402
/// - W. T. Reeves, D. H. Salesin and R. L. Cook, "Rendering Antialiased Shadows with Depth Maps",
///   SIGGRAPH '87, 283–291, 1987. https://doi.org/10.1145/37402.37435
/// - D. Holbert, "Saying 'Goodbye' to Shadow Acne", GDC 2011 poster (normal offset shadows).
/// - J. Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
///   Advances in Real-Time Rendering course (interleaved gradient noise).
class TerrainRenderer {
  public:
    static constexpr int kMaxCascades = 4;

    /// Builds the pipelines for render passes with the given colour and depth formats.
    TerrainRenderer(const GpuContext& gpu, WGPUTextureFormat colorFormat,
                    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float);
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    /// Uploads `raster` as the heightfield to draw.
    /// @throws std::invalid_argument if the raster is smaller than 2 × 2, larger than the device's
    /// maximum texture size, or not north-up with square pixels.
    void SetHeightfield(const Raster<float>& raster);

    bool HasHeightfield() const {
        return heightTexture_ != nullptr;
    }

    std::size_t Width() const {
        return width_;
    }

    std::size_t Height() const {
        return height_;
    }

    std::uint64_t TriangleCount() const;

    /// GPU memory held for the heightfield, shadow maps and uniforms, in bytes.
    std::uint64_t GpuBytes() const;

    /// World-space bounds of the drawn mesh at the given vertical exaggeration.
    std::pair<glm::vec3, glm::vec3> Bounds(float exaggeration) const;

    /// Uploads per-frame uniforms and fits the shadow cascades. Call once per frame before
    /// `EncodeShadows` and `Draw`.
    void Update(const TerrainDrawParams& params);

    /// Records the shadow map passes. Does nothing without a heightfield or with shadows off.
    void EncodeShadows(WGPUCommandEncoder encoder) const;

    /// Records the terrain draw into `pass`. Does nothing without a heightfield.
    void Draw(WGPURenderPassEncoder pass) const;

    /// Cascades fitted by the last `Update`; empty with shadows off.
    const std::vector<ShadowCascade>& Cascades() const {
        return cascades_;
    }

  private:
    void CreateShadowMaps(std::uint32_t resolution);
    void ReleaseShadowMaps();
    void ReleaseHeightfield();

    const GpuContext& gpu_;
    WGPUBindGroupLayout terrainLayout_ = nullptr;
    WGPUBindGroupLayout shadowSamplingLayout_ = nullptr;
    WGPUBindGroupLayout lightLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPURenderPipeline shadowPipeline_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer shadowUniformBuffer_ = nullptr;
    WGPUBuffer lightBuffer_ = nullptr;
    WGPUBindGroup lightBindGroup_ = nullptr;
    WGPUSampler shadowSampler_ = nullptr;

    WGPUTexture heightTexture_ = nullptr;
    WGPUTextureView heightView_ = nullptr;
    WGPUBindGroup terrainBindGroup_ = nullptr;

    WGPUTexture shadowTexture_ = nullptr;
    WGPUTextureView shadowArrayView_ = nullptr;
    WGPUTextureView shadowLayerViews_[kMaxCascades] = {};
    WGPUBindGroup shadowBindGroup_ = nullptr;
    std::uint32_t shadowResolution_ = 0;

    std::vector<ShadowCascade> cascades_;
    std::vector<glm::vec2> poisson_;
    bool shadowsEnabled_ = false;

    std::size_t width_ = 0;
    std::size_t height_ = 0;
    double originX_ = 0.0;
    double originY_ = 0.0;
    double pixelSize_ = 1.0;
    float minHeight_ = 0.0f;
    float maxHeight_ = 0.0f;
};

}  // namespace terraingen::render
