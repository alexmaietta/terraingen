#pragma once

#include "ernie/camera/orbit_camera.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/raster/shadow_cascades.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace ernie {

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

/// Per-frame parameters every part shares: the view, the sun and the shadows.
struct FrameParams {
    CameraView camera;
    glm::vec3 sunDirection{0.0f, 0.0f, 1.0f};  ///< Unit vector pointing towards the sun.
    float ambient = 0.15f;                     ///< Fraction of light reaching unlit surfaces.
    float exposure = 1.0f;                     ///< Scale on reflected sunlight.
    bool wireframe = false;                    ///< Draw triangle edges over every part.
    ShadowSettings shadows;
};

/// Axis-aligned world bounds, low and high corners.
using Bounds3 = std::pair<glm::vec3, glm::vec3>;

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
/// frame the renderer asks every part for its bounds, fits the shadow cascades around them, lets
/// each part prepare for the final view and cascades, and then has each record its draws into
/// every shadow cascade and into the main pass. Parts bind the frame's resources at bindings 0 to
/// 4, a material's at 5 to 9 (`RasterRenderer::Material`) and their own from 10.
class RasterPart {
  public:
    virtual ~RasterPart() = default;

    /// World bounds of what the part draws, for fitting shadow cascades; none if nothing.
    virtual std::optional<Bounds3> Bounds() const = 0;

    /// Prepares the next frame once the view and cascades are final, for example selecting levels
    /// of detail for the main view and each cascade. Called by `RasterRenderer::Update`, only
    /// while no submitted frame is reading the part's buffers.
    virtual void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) = 0;

    /// Records depth-only draws for shadow cascade `cascade` into the open rendering pass.
    virtual void RecordShadow(VkCommandBuffer commands, const RasterFrame& frame,
                              std::size_t cascade, const glm::mat4& lightViewProjection) const = 0;

    /// Records colour draws into the open main rendering pass.
    virtual void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const = 0;
};

}  // namespace ernie
