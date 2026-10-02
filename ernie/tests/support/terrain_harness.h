#pragma once

#include "ernie/raster/raster_renderer.h"
#include "ernie/raster/terrain_part.h"

#include <memory>
#include <utility>

namespace ernie::testing {

/// Everything one terrain frame needs, gathered as the terrain tests set it up.
struct TerrainDrawParams {
    /// The terrain's material settings plus the frame's exposure.
    struct Surface : MaterialSettings {
        float exposure = 1.0f;
    };

    CameraView camera;
    VkExtent2D extent{64, 64};  ///< Image size, which `Record`'s target must have.
    glm::vec3 sunDirection{0.0f, 0.0f, 1.0f};
    float ambient = 0.15f;
    float exaggeration = 1.0f;
    TerrainColorMode colorMode = TerrainColorMode::ShadedColormap;
    bool wireframe = false;
    ShadowSettings shadows;
    LodSettings lod;
    Surface surface;
};

/// A `RasterRenderer` drawing one `TerrainPart` with the default material, driven the way
/// `RasterDelegate` drives them, behind the calls the terrain tests make.
class TerrainRenderer {
  public:
    explicit TerrainRenderer(const VulkanContext& context, VkFormat colorFormat,
                             VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
                             BrdfRegistry brdfs = BrdfRegistry::WithBuiltins())
        : context_(context), renderer_(context, colorFormat, std::move(brdfs)),
          terrain_(renderer_) {
        static_cast<void>(depthFormat);
    }

    void SetHeightfield(const RasterView& heights) {
        terrain_.SetHeightfield(heights);
    }

    bool HasHeightfield() const {
        return terrain_.HasHeightfield();
    }

    std::size_t Width() const {
        return terrain_.Width();
    }

    std::size_t Height() const {
        return terrain_.Height();
    }

    void Update(const TerrainDrawParams& params) {
        renderer_.SetMaterial(RasterRenderer::kDefaultMaterial, params.surface);
        terrain_.SetParams(
            {params.exaggeration, params.colorMode, params.lod, RasterRenderer::kDefaultMaterial});
        FrameParams frame;
        frame.camera = params.camera;
        frame.physical.width = params.extent.width;
        frame.physical.height = params.extent.height;
        frame.sunDirection = params.sunDirection;
        frame.ambient = params.ambient;
        frame.exposure = params.surface.exposure;
        frame.wireframe = params.wireframe;
        frame.shadows = params.shadows;
        RasterPart* parts[] = {&terrain_};
        renderer_.Update(frame, parts);
    }

    /// Renders the frame and draws its LdrColor into `target.color`.
    void Record(VkCommandBuffer commands, const RenderingTarget& target) {
        if (targets_ == nullptr || targets_->Extent().width != target.extent.width ||
            targets_->Extent().height != target.extent.height ||
            targets_->Bands() != renderer_.Bands().count) {
            targets_ = std::make_unique<RenderTargets>(context_, target.extent,
                                                       renderer_.Bands().count, false);
        }
        const RasterPart* parts[] = {&terrain_};
        renderer_.Record(commands, *targets_, parts, &target);
    }

    RenderTargets* Targets() {
        return targets_.get();
    }

    RasterMaterial& Material() {
        return renderer_.Material(RasterRenderer::kDefaultMaterial);
    }

    const BrdfRegistry& Brdfs() const {
        return renderer_.Brdfs();
    }

    void AddBrdf(BrdfModel model) {
        renderer_.AddBrdf(std::move(model));
    }

    std::pair<glm::vec3, glm::vec3> Bounds(float exaggeration) const {
        return terrain_.Bounds(exaggeration);
    }

    std::uint64_t TriangleCount() const {
        return terrain_.TriangleCount();
    }

    std::size_t PatchCount() const {
        return terrain_.PatchCount();
    }

    float LodFirstRange() const {
        return terrain_.LodFirstRange();
    }

    const LodQuadtree& Lod() const {
        return terrain_.Lod();
    }

    const std::vector<ShadowCascade>& Cascades() const {
        return renderer_.Cascades();
    }

    std::uint64_t GpuBytes() const {
        return renderer_.GpuBytes() + terrain_.GpuBytes();
    }

  private:
    const VulkanContext& context_;
    RasterRenderer renderer_;
    TerrainPart terrain_;
    std::unique_ptr<RenderTargets> targets_;
};

}  // namespace ernie::testing
