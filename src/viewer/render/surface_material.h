#pragma once

#include "render/gpu_context.h"
#include "render/image.h"
#include "terraingen/core/raster.h"

#include <glm/glm.hpp>
#include <webgpu/webgpu.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace terraingen::render {

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

/// How the terrain surface reflects light.
struct SurfaceSettings {
    std::string brdf = "lambert";   ///< Id of a model in the renderer's `BrdfRegistry`.
    std::vector<float> brdfParams;  ///< In the model's parameter order; missing ones use defaults.
    glm::vec3 albedo{1.0f};         ///< Albedo input where no albedo map applies.
    float exposure = 1.0f;          ///< Scale on the reflected radiance factor.
    bool useAlbedoMap = true;       ///< Use the albedo map when one is set.
    DetailSettings detail;
};

/// GPU resources for the terrain's surface: the material uniform, an optional albedo map and
/// optional tileable detail albedo and normal textures, bound as group 2 of the colour pass
/// (`surface.wgsl`).
///
/// The albedo map is a georeferenced raster sampled by world position, like the heightfield but
/// with its own grid. Detail textures repeat every `DetailSettings::tileSize` world units; the
/// detail albedo multiplies the base albedo after division by its own mean, so it adds variation
/// without changing the map's calibrated mean, and the detail normal map's slopes add to the
/// terrain's. Detail textures are mipmapped and sampled anisotropically.
///
/// References:
/// - M. S. Mikkelsen, "Practical Real-Time Hex-Tiling", Journal of Computer Graphics Techniques
///   11(3), 2022.
/// - M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", Journal of Computer Graphics
///   Techniques 9(3), 2020 (pcg2d, for per-tile rotations and offsets).
class SurfaceMaterial {
  public:
    explicit SurfaceMaterial(const GpuContext& gpu);
    ~SurfaceMaterial();
    SurfaceMaterial(const SurfaceMaterial&) = delete;
    SurfaceMaterial& operator=(const SurfaceMaterial&) = delete;

    WGPUBindGroupLayout Layout() const {
        return layout_;
    }

    WGPUBindGroup BindGroup() const {
        return bindGroup_;
    }

    /// Uploads `albedo` as the albedo map. Cells that are not finite or equal the nodata value
    /// fall back to `SurfaceSettings::albedo`.
    /// @throws std::invalid_argument if the raster is empty, not north-up with square pixels, or
    /// larger than the device's maximum texture size.
    void SetAlbedoMap(const Raster<float>& albedo);
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

    /// Writes the material uniform; `brdfParams` are the active model's resolved values.
    void Update(const SurfaceSettings& settings, std::span<const float> brdfParams);

    /// GPU memory held for textures and the uniform, in bytes.
    std::uint64_t GpuBytes() const;

  private:
    void CheckDetailImage(const Image* image) const;
    void CreateBindGroup();
    void ReplaceTexture(WGPUTexture& texture, WGPUTextureView& view, WGPUTexture next);
    WGPUTexture UploadMips(const Image& image, MipFilter filter, ImageEncoding encoding,
                           WGPUTextureFormat format, const char* label, std::uint64_t& bytes);

    const GpuContext& gpu_;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    WGPUTexture albedoMap_ = nullptr;
    WGPUTextureView albedoMapView_ = nullptr;
    WGPUTexture detailAlbedo_ = nullptr;
    WGPUTextureView detailAlbedoView_ = nullptr;
    WGPUTexture detailNormal_ = nullptr;
    WGPUTextureView detailNormalView_ = nullptr;

    bool hasAlbedoMap_ = false;
    bool hasDetailAlbedo_ = false;
    bool hasDetailNormal_ = false;
    glm::vec3 detailMean_{1.0f};
    glm::dvec4 albedoGrid_{0.0};  ///< Origin x, origin y, pixel size, unused.
    glm::uvec2 albedoSize_{1u, 1u};
    std::uint64_t albedoMapBytes_ = 0;
    std::uint64_t detailAlbedoBytes_ = 0;
    std::uint64_t detailNormalBytes_ = 0;
};

}  // namespace terraingen::render
