#pragma once

#include "ernie/brdf.h"
#include "ernie/frustum.h"
#include "ernie/gpu_context.h"
#include "ernie/lod_quadtree.h"
#include "ernie/orbit_camera.h"
#include "ernie/raster_view.h"
#include "ernie/shadow_cascades.h"
#include "ernie/surface_material.h"

#include <glm/glm.hpp>
#include <webgpu/webgpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ernie {

/// How terrain fragments are coloured.
enum class TerrainColorMode : std::uint32_t {
    Hillshade = 0,       ///< Surface albedo lit by the selected reflectance model.
    Colormap = 1,        ///< Height colormap without shading.
    ShadedColormap = 2,  ///< Height colormap modulated by the lit surface.
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

/// Continuous level-of-detail options; see `LodQuadtree`.
struct LodSettings {
    bool enabled = true;       ///< Off draws every raster cell at full resolution.
    float detail = 4.0f;       ///< First LOD range in level-0 node widths; raised to the crack-free
                               ///< minimum (`LodQuadtree::MinFirstRange`) when lower.
    float morphStart = 0.67f;  ///< Fraction of each level's distance band before morphing starts,
                               ///< clamped to [0.05, 0.95].
    bool freeze = false;       ///< Keep selecting from the eye and frustum of the frame this was
                               ///< turned on, to inspect the selection from elsewhere.
    bool showLevels = false;   ///< Tint patches by LOD level.
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
    LodSettings lod;
    SurfaceSettings surface;
};

/// Draws a north-up heightfield raster as a lit, shadowed triangle mesh.
///
/// Heights live in an `R32Float` texture that the vertex shader reads; the mesh has no vertex or
/// index buffers. Each frame `LodQuadtree` selects square patches at continuous levels of detail
/// (CDLOD), and each patch is one instance of a shared grid whose vertices are derived from the
/// vertex and instance indices and morphed between levels. Normals come from a precomputed
/// central-difference gradient texture sampled per fragment, so shading is independent of the mesh
/// level. The world frame is the raster's: x east, y north, z up.
///
/// Fragments are lit by a reflectance model from a `BrdfRegistry`: the colour pass is composed
/// from the shared terrain code, the model's WGSL and `surface.wgsl`, with one pipeline per model
/// built on first use. Albedo comes from `Material()`'s albedo map or a constant, varied by its
/// detail albedo texture; its detail normal texture perturbs the shading normal.
///
/// The main view culls patches against the camera frustum and each shadow cascade against its
/// light frustum, but all of them choose levels and morph from the camera's eye, so shadow casters
/// match the visible surface.
///
/// Shadows use cascaded shadow maps (`FitCascades`) filtered with rotated Poisson-disk PCF, with
/// slope-scaled depth bias and normal offset against acne.
///
/// References:
/// - F. Strugar, "Continuous Distance-Dependent Level of Detail for Rendering Heightmaps", Journal
///   of Graphics, GPU, and Game Tools 14(4):57-74, 2009.
///   https://doi.org/10.1080/2151237X.2009.10129287
/// - L. Williams, "Casting Curved Shadows on Curved Surfaces", SIGGRAPH '78, 270-274, 1978.
///   https://doi.org/10.1145/965139.807402
/// - W. T. Reeves, D. H. Salesin and R. L. Cook, "Rendering Antialiased Shadows with Depth Maps",
///   SIGGRAPH '87, 283-291, 1987. https://doi.org/10.1145/37402.37435
/// - D. Holbert, "Saying 'Goodbye' to Shadow Acne", GDC 2011 poster (normal offset shadows).
/// - J. Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
///   Advances in Real-Time Rendering course (interleaved gradient noise).
class TerrainRenderer {
  public:
    static constexpr int kMaxCascades = 4;
    /// Grid quads along a level-0 LOD node; patches are half this.
    static constexpr std::uint32_t kLodLeafCells = 64;

    /// Builds the pipelines for render passes with the given colour and depth formats, drawing
    /// with the reflectance models in `brdfs`.
    TerrainRenderer(const GpuContext& gpu, WGPUTextureFormat colorFormat,
                    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float,
                    BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    /// Uploads `heights` as the heightfield to draw; the data is copied, so the view need not
    /// outlive the call.
    /// @throws std::invalid_argument if the view is smaller than 2 x 2, malformed (see
    /// `RasterView`), or larger than the device's maximum texture size.
    void SetHeightfield(const RasterView& heights);

    bool HasHeightfield() const {
        return heightTexture_ != nullptr;
    }

    std::size_t Width() const {
        return width_;
    }

    std::size_t Height() const {
        return height_;
    }

    /// Triangles drawn in the main view by the last `Update`.
    std::uint64_t TriangleCount() const {
        return triangles_;
    }

    /// LOD patches drawn in the main view by the last `Update`.
    std::size_t PatchCount() const {
        return mainRange_.count;
    }

    /// First LOD range used by the last `Update`, in world units; 0 with LOD off.
    float LodFirstRange() const {
        return firstRange_;
    }

    const LodQuadtree& Lod() const {
        return tree_;
    }

    const BrdfRegistry& Brdfs() const {
        return brdfs_;
    }

    /// Compiles `model` into a colour pipeline and registers it.
    /// @throws std::invalid_argument if the registry rejects the model (`BrdfRegistry::Add`) or
    /// its WGSL fails to compile; the message includes the compiler's diagnostics.
    void AddBrdf(BrdfModel model);

    /// Albedo map and detail textures.
    SurfaceMaterial& Material() {
        return surface_;
    }

    const SurfaceMaterial& Material() const {
        return surface_;
    }

    /// GPU memory held for the heightfield, shadow maps and uniforms, in bytes.
    std::uint64_t GpuBytes() const;

    /// World-space bounds of the drawn mesh at the given vertical exaggeration.
    std::pair<glm::vec3, glm::vec3> Bounds(float exaggeration) const;

    /// Uploads per-frame uniforms, fits the shadow cascades and selects the LOD patches for the
    /// main view and every cascade. Call once per frame before `EncodeShadows` and `Draw`.
    /// @throws std::invalid_argument if `params.surface.brdf` is not in `Brdfs()`.
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
    struct DrawRange {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    WGPURenderPipeline CreateColorPipeline(const BrdfModel& model) const;
    void CreateShadowMaps(std::uint32_t resolution);
    void CreateTerrainBindGroup();
    void ReservePatches(std::size_t count);
    void DrawPatches(WGPURenderPassEncoder pass, DrawRange range) const;
    void ReleaseShadowMaps();
    void ReleaseHeightfield();

    const GpuContext& gpu_;
    SurfaceMaterial surface_;
    BrdfRegistry brdfs_;
    WGPUTextureFormat colorFormat_;
    WGPUTextureFormat depthFormat_;
    WGPUPipelineLayout colorLayout_ = nullptr;
    std::map<std::string, WGPURenderPipeline, std::less<>> colorPipelines_;
    WGPURenderPipeline activePipeline_ = nullptr;
    WGPUBindGroupLayout terrainLayout_ = nullptr;
    WGPUBindGroupLayout shadowSamplingLayout_ = nullptr;
    WGPUBindGroupLayout lightLayout_ = nullptr;
    WGPURenderPipeline shadowPipeline_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer shadowUniformBuffer_ = nullptr;
    WGPUBuffer lightBuffer_ = nullptr;
    WGPUBindGroup lightBindGroup_ = nullptr;
    WGPUSampler shadowSampler_ = nullptr;

    WGPUTexture heightTexture_ = nullptr;
    WGPUTextureView heightView_ = nullptr;
    WGPUTexture gradientTexture_ = nullptr;
    WGPUTextureView gradientView_ = nullptr;
    WGPUSampler gradientSampler_ = nullptr;
    WGPUBindGroup terrainBindGroup_ = nullptr;

    LodQuadtree tree_;
    WGPUBuffer patchBuffer_ = nullptr;
    std::size_t patchCapacity_ = 0;
    std::vector<LodPatch> patches_;
    DrawRange mainRange_;
    std::array<DrawRange, kMaxCascades> cascadeRanges_{};
    std::uint64_t triangles_ = 0;
    float firstRange_ = 0.0f;
    bool frozen_ = false;
    glm::vec3 lodEye_{0.0f};
    Frustum lodFrustum_;

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

}  // namespace ernie
