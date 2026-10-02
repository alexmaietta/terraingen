#pragma once

#include "ernie/core/raster_view.h"
#include "ernie/geometry/frustum.h"
#include "ernie/geometry/lod_quadtree.h"
#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/raster/raster_part.h"
#include "ernie/raster/raster_renderer.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ernie {

/// How terrain fragments are coloured.
enum class TerrainColorMode : std::uint32_t {
    Hillshade = 0,       ///< Material albedo lit by the material's reflectance model.
    Colormap = 1,        ///< Height colormap without shading.
    ShadedColormap = 2,  ///< Height colormap modulated by the lit surface.
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

/// Terrain options for the next frames.
struct TerrainParams {
    float exaggeration = 1.0f;  ///< Vertical exaggeration of heights.
    TerrainColorMode colorMode = TerrainColorMode::ShadedColormap;
    LodSettings lod;
    std::uint32_t material = RasterRenderer::kDefaultMaterial;  ///< Id in the renderer's table.
};

/// A north-up heightfield raster drawn as a lit, shadowed triangle mesh: a `RasterPart`.
///
/// Heights live in an `R32_SFLOAT` image that the vertex shader reads; the mesh has no vertex or
/// index buffers. Each frame `LodQuadtree` selects square patches at continuous levels of detail
/// (CDLOD), and each patch is one instance of a shared grid whose vertices are derived from the
/// vertex and instance indices and morphed between levels. Normals come from a precomputed
/// central-difference gradient texture sampled per fragment, so shading is independent of the mesh
/// level. The world frame is the raster's: x east, y north, z up.
///
/// Fragments are lit by the reflectance model of the terrain's material: the colour pass is
/// composed in Slang from `terrain.slang`, `surface.slang`, the model's module and
/// `terrain_color.slang`, with one Vulkan pipeline per model built on first use. Albedo comes from
/// the material's albedo map or constant, varied by its detail albedo texture; its detail normal
/// texture perturbs the shading normal.
///
/// The main view culls patches against the camera frustum and each shadow cascade against its
/// light frustum, but all of them choose levels and morph from the camera's eye, so shadow casters
/// match the visible surface.
///
/// Reference: F. Strugar, "Continuous Distance-Dependent Level of Detail for Rendering
/// Heightmaps", Journal of Graphics, GPU, and Game Tools 14(4):57-74, 2009.
/// https://doi.org/10.1080/2151237X.2009.10129287
class TerrainPart : public RasterPart {
  public:
    /// Grid quads along a level-0 LOD node; patches are half this.
    static constexpr std::uint32_t kLodLeafCells = 64;

    /// A terrain drawn by `renderer`, which must outlive it.
    /// @throws std::runtime_error if the shadow pipeline cannot be created.
    explicit TerrainPart(RasterRenderer& renderer);
    ~TerrainPart() override;
    TerrainPart(const TerrainPart&) = delete;
    TerrainPart& operator=(const TerrainPart&) = delete;

    /// Uploads `heights` as the heightfield to draw; the data is copied, so the view need not
    /// outlive the call.
    /// @throws std::invalid_argument if the view is smaller than 2 x 2, malformed (see
    /// `RasterView`), or larger than the device's maximum texture size.
    void SetHeightfield(const RasterView& heights);

    /// Drops the heightfield, so the part draws nothing. Waits for the device to go idle.
    void ClearHeightfield();

    bool HasHeightfield() const {
        return heights_ != nullptr;
    }

    std::size_t Width() const {
        return width_;
    }

    std::size_t Height() const {
        return height_;
    }

    /// Options for the next frames.
    void SetParams(const TerrainParams& params) {
        params_ = params;
    }

    const TerrainParams& Params() const {
        return params_;
    }

    /// Triangles drawn in the main view by the last frame.
    std::uint64_t TriangleCount() const {
        return triangles_;
    }

    /// LOD patches drawn in the main view by the last frame.
    std::size_t PatchCount() const {
        return mainRange_.count;
    }

    /// First LOD range used by the last frame, in world units; 0 with LOD off.
    float LodFirstRange() const {
        return firstRange_;
    }

    const LodQuadtree& Lod() const {
        return tree_;
    }

    /// GPU memory held for the heightfield and the part's buffers, in bytes.
    std::uint64_t GpuBytes() const;

    /// World-space bounds of the drawn mesh at the given vertical exaggeration.
    std::pair<glm::vec3, glm::vec3> Bounds(float exaggeration) const;

    /// Bounds at the current exaggeration; none without a heightfield.
    std::optional<Bounds3> Bounds() const override;

    /// Writes the terrain uniforms and selects the LOD patches for the main view and every cascade.
    /// @throws std::invalid_argument if the material's colour pipeline fails to compile.
    void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) override;

    void RecordShadow(VkCommandBuffer commands, const RasterFrame& frame, std::size_t cascade,
                      const glm::mat4& lightViewProjection) const override;
    void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const override;

  private:
    struct DrawRange {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    const GraphicsPipeline& ColorPipeline(const BrdfModel& model);
    void ReservePatches(std::size_t count);
    std::vector<DescriptorResource> Resources(const RasterFrame& frame) const;
    void DrawPatches(VkCommandBuffer commands, DrawRange range, const glm::mat4& light) const;

    RasterRenderer& renderer_;
    PipelineLayout layout_;
    std::map<std::string, std::unique_ptr<GraphicsPipeline>, std::less<>> colorPipelines_;
    const GraphicsPipeline* activePipeline_ = nullptr;
    std::unique_ptr<GraphicsPipeline> shadowPipeline_;
    TerrainParams params_;

    GpuBuffer uniforms_;
    std::unique_ptr<GpuImage> heights_;
    std::unique_ptr<GpuImage> gradients_;
    std::unique_ptr<GpuBuffer> patchBuffer_;

    LodQuadtree tree_;
    std::vector<LodPatch> patches_;
    DrawRange mainRange_;
    std::array<DrawRange, RasterRenderer::kMaxCascades> cascadeRanges_{};
    std::uint64_t triangles_ = 0;
    float firstRange_ = 0.0f;
    bool frozen_ = false;
    glm::vec3 lodEye_{0.0f};
    Frustum lodFrustum_;
    bool prepared_ = false;

    std::size_t width_ = 0;
    std::size_t height_ = 0;
    double originX_ = 0.0;
    double originY_ = 0.0;
    double pixelSize_ = 1.0;
    float minHeight_ = 0.0f;
    float maxHeight_ = 0.0f;
};

}  // namespace ernie
