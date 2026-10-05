#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/raster/raster_part.h"
#include "ernie/raster/raster_renderer.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace ernie {

/// Options of the ground plane.
struct GroundParams {
    float height = 0.0f;                                        ///< World z of the plane.
    std::uint32_t material = RasterRenderer::kDefaultMaterial;  ///< Id in the renderer's table.
    std::uint32_t primId = 1;  ///< Written to the PrimId render var; 0 means background.
    bool checker = false;      ///< Modulate the material's albedo with a checkerboard.
    float checkerSize = 1.0f;  ///< World size of a checker square.
    /// Linear colours of the squares, multiplying the material's albedo.
    std::array<glm::vec3, 2> checkerColors{glm::vec3(1.0f), glm::vec3(0.5f)};
};

/// An infinite horizontal plane at z = `height` facing up: a `RasterPart` drawn as a full-screen
/// pass that intersects each pixel's camera ray with the plane and writes the hit's depth, so
/// terrain and meshes sort against it and it reaches the horizon (hits past the far plane are
/// drawn at the far plane). It is lit and shadowed like any surface, with its material's
/// reflectance model, albedo map and detail textures over world x and y, and an optional
/// box-filtered checkerboard (`ground.slang`). It casts no shadows and has no bounds, so it does
/// not stretch the shadow cascades.
class GroundPart : public RasterPart {
  public:
    /// A ground plane drawn by `renderer`, which must outlive it; disabled until `SetEnabled`.
    /// @throws std::runtime_error if its pipeline cannot be created.
    explicit GroundPart(RasterRenderer& renderer);
    ~GroundPart() override;
    GroundPart(const GroundPart&) = delete;
    GroundPart& operator=(const GroundPart&) = delete;

    void SetEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool Enabled() const {
        return enabled_;
    }

    /// Options for the next frames.
    /// @throws std::invalid_argument for a checker size that is not positive and finite.
    void SetParams(const GroundParams& params);

    const GroundParams& Params() const {
        return params_;
    }

    std::optional<Bounds3> Bounds() const override {
        return std::nullopt;
    }

    void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) override;

    void RecordShadow(VkCommandBuffer, const RasterFrame&, std::size_t,
                      const glm::mat4&) const override {}

    void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const override;

  private:
    RasterRenderer& renderer_;
    PipelineLayout layout_;
    std::unique_ptr<GraphicsPipeline> pipeline_;
    GpuBuffer uniforms_;
    GroundParams params_;
    bool enabled_ = false;
    bool prepared_ = false;
};

}  // namespace ernie
