#pragma once

#include "ernie/gpu/pipeline.h"
#include "ernie/raster/render_targets.h"
#include "ernie/raster/shadow_cascades.h"
#include "ernie/render/frame_params.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ernie {

/// The per-frame resources every raster pass binds (`frame.slang`): view and light uniforms,
/// shadow uniforms, cascaded shadow maps, their comparison sampler and a clamping linear sampler,
/// at bindings 0 to 4.
struct RasterFrame {
    DescriptorResource uniforms;
    DescriptorResource shadows;
    DescriptorResource shadowMap;
    DescriptorResource shadowSampler;
    DescriptorResource clampSampler;
};

/// Something a `RasterRenderer` draws: terrain, meshes, and in future volumes or points. Each
/// frame the renderer asks every part for its bounds, fits the shadow views around them (the
/// sun's cascades, then the views of the other lights' shadow maps), lets each part prepare for
/// the final view and shadow views, and then has each record its draws into every shadow view and
/// into the main pass. Parts bind the frame's resources at bindings 0 to
/// 4, a material's at 5 to 9 (`RasterRenderer::Material`) and their own from 10.
class RasterPart {
  public:
    virtual ~RasterPart() = default;

    /// World bounds of what the part draws, for fitting shadow cascades; none if nothing.
    virtual std::optional<Bounds3> Bounds() const = 0;

    /// Prepares the next frame once the view and shadow views are final, for example selecting
    /// levels of detail for the main view and each shadow view (orthographic or perspective; only
    /// their `lightViewProjection` applies to every view). Called by `RasterRenderer::Update`,
    /// only while no submitted frame is reading the part's buffers.
    virtual void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) = 0;

    /// Records depth-only draws for shadow view `cascade` (an index into the views given to
    /// `Prepare`) into the open rendering pass.
    virtual void RecordShadow(VkCommandBuffer commands, const RasterFrame& frame,
                              std::size_t cascade, const glm::mat4& lightViewProjection) const = 0;

    /// Records colour draws into the open main rendering pass.
    virtual void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const = 0;
};

}  // namespace ernie
