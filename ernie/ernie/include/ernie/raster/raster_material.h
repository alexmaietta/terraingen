#pragma once

#include "ernie/core/image.h"
#include "ernie/core/raster_view.h"
#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ernie {

/// Tiling of the detail albedo and normal textures.
struct DetailSettings {
    float tileSize = 4.0f;        ///< World units covered by one repeat of the textures.
    float albedoStrength = 1.0f;  ///< 0 ignores the detail albedo, 1 applies it fully.
    float normalStrength = 1.0f;  ///< Scale on the detail normal map's slopes.
    bool directXNormals = false;  ///< Green points down the image (DirectX) rather than up.
    bool hexTiling = true;        ///< Hide repetition with hex-tiling (Mikkelsen 2022).
    float hexRotation = 1.0f;     ///< Random tile rotation, 0 none to 1 any angle.
    float hexContrast = 0.6f;     ///< Weight of sample luminance in the tile blend, 0 to 1.
    float hexSharpness = 7.0f;    ///< Exponent on the barycentric blend weights.
};

/// How a surface reflects light: a material's settings.
struct MaterialSettings {
    std::string brdf = "lambert";   ///< Id of a model in the renderer's `BrdfRegistry`.
    std::vector<float> brdfParams;  ///< In the model's parameter order; missing ones use defaults.
    glm::vec3 albedo{1.0f};         ///< Albedo input where no albedo map applies.
    bool useAlbedoMap = true;       ///< Use the albedo map when one is set.
    DetailSettings detail;
};

/// A material's GPU resources: the material uniform, an optional albedo map and optional tileable
/// detail albedo and normal textures, bound at bindings 5 to 9 of every raster pass
/// (`surface.slang`), so terrain and meshes share materials.
///
/// The albedo map is a georeferenced raster sampled by world x and y, like the heightfield but
/// with its own grid. Detail textures repeat every `DetailSettings::tileSize` world units: the
/// terrain projects them from above and meshes along all three axes (triplanar); the
/// detail albedo multiplies the base albedo after division by its own mean, so it adds variation
/// without changing the map's calibrated mean, and the detail normal map's slopes add to the
/// terrain's. Detail textures are mipmapped and sampled anisotropically.
///
/// References:
/// - M. S. Mikkelsen, "Practical Real-Time Hex-Tiling", Journal of Computer Graphics Techniques
///   11(3), 2022.
/// - M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", Journal of Computer Graphics
///   Techniques 9(3), 2020 (pcg2d, for per-tile rotations and offsets).
class RasterMaterial {
  public:
    explicit RasterMaterial(const VulkanContext& context);
    RasterMaterial(const RasterMaterial&) = delete;
    RasterMaterial& operator=(const RasterMaterial&) = delete;

    /// The material's uniform buffer, albedo map, detail albedo, detail normal and detail sampler,
    /// in binding order (5 to 9).
    void AppendResources(std::vector<DescriptorResource>& resources) const;

    /// Uploads `albedo` as the albedo map; the data is copied, so the view need not outlive the
    /// call. Cells that are not finite or equal the nodata value fall back to
    /// `MaterialSettings::albedo`.
    /// @throws std::invalid_argument if the view is empty or malformed (see `RasterView`), or
    /// larger than the device's maximum texture size.
    void SetAlbedoMap(const RasterView& albedo);
    void ClearAlbedoMap();

    bool HasAlbedoMap() const {
        return hasAlbedoMap_;
    }

    /// Uploads detail textures; nullptr removes one. `albedo` holds linear colour (load it with
    /// `ImageEncoding::Srgb`); `normal` holds tangent-space normals encoded as n / 2 + 1/2 (load
    /// with `ImageEncoding::Linear`), x east and y up the image.
    /// @throws std::invalid_argument if an image is empty or larger than the device's maximum
    /// texture size.
    void SetDetailTextures(const Image* albedo, const Image* normal);

    /// Replaces only the detail albedo texture; nullptr removes it.
    /// @throws std::invalid_argument as `SetDetailTextures`.
    void SetDetailAlbedo(const Image* albedo);

    /// Replaces only the detail normal map; nullptr removes it.
    /// @throws std::invalid_argument as `SetDetailTextures`.
    void SetDetailNormal(const Image* normal);

    bool HasDetailAlbedo() const {
        return hasDetailAlbedo_;
    }

    bool HasDetailNormal() const {
        return hasDetailNormal_;
    }

    /// Mean linear colour of the detail albedo texture as stored (after 8-bit sRGB quantisation),
    /// (1, 1, 1) without one.
    glm::vec3 DetailAlbedoMean() const {
        return detailMean_;
    }

    /// Sets the material's settings; `brdfParams` are the model's resolved values. This and the
    /// texture setters write the material's uniform, so call them only while no submitted work is
    /// reading the material.
    void Update(const MaterialSettings& settings, std::span<const float> brdfParams);

    /// The settings of the last `Update`.
    const MaterialSettings& Settings() const {
        return settings_;
    }

    /// GPU memory held for textures and the uniform, in bytes.
    std::uint64_t GpuBytes() const;

  private:
    void CheckDetailImage(const Image* image) const;
    void WriteUniform();
    std::unique_ptr<GpuImage> UploadMips(const Image& image, MipFilter filter,
                                         ImageEncoding encoding, VkFormat format) const;

    const VulkanContext& context_;
    MaterialSettings settings_;
    std::vector<float> params_;
    GpuBuffer uniform_;
    Sampler sampler_;
    std::unique_ptr<GpuImage> albedoMap_;
    std::unique_ptr<GpuImage> detailAlbedo_;
    std::unique_ptr<GpuImage> detailNormal_;

    bool hasAlbedoMap_ = false;
    bool hasDetailAlbedo_ = false;
    bool hasDetailNormal_ = false;
    glm::vec3 detailMean_{1.0f};
    glm::dvec4 albedoGrid_{0.0};  ///< Origin x, origin y, pixel size, unused.
    glm::uvec2 albedoSize_{1u, 1u};
};

}  // namespace ernie
