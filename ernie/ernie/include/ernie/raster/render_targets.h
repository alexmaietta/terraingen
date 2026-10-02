#pragma once

#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/vulkan_context.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernie {

/// Render var names, after Omniverse's where it has the quantity.
namespace render_vars {
inline constexpr std::string_view kLdrColor = "LdrColor";  ///< uint8 sRGB (rows, cols, 4).
inline constexpr std::string_view kHdrColor = "HdrColor";  ///< float32 linear sRGB, cd/m^2.
/// float32 spectral radiance at each sensor pixel, W m^-2 sr^-1 nm^-1, (bands, rows, cols).
inline constexpr std::string_view kSpectralRadiance = "SpectralRadiance";
inline constexpr std::string_view kElectrons = "Electrons";  ///< float32 per sensor channel.
inline constexpr std::string_view kRawDn = "RawDN";          ///< uint16 per sensor channel.
inline constexpr std::string_view kDistanceToImagePlane = "DistanceToImagePlane";  ///< float32.
inline constexpr std::string_view kDistanceToCamera = "DistanceToCamera";          ///< float32.
inline constexpr std::string_view kNormal = "Normal";  ///< float32 world shading normal (xyz).
inline constexpr std::string_view kPrimId = "PrimId";  ///< uint32, 0 for background.
inline constexpr std::string_view kInstanceId = "InstanceId";  ///< uint32 instance index.
}  // namespace render_vars

/// The built-in sensor filters a render product runs in order on its spectral radiance, as
/// Chrono::Sensor's filter lists:
/// - `Lens`: resamples the radiance into sensor pixels through the inverse lens distortion, with
///   cos^4 vignetting; writes SpectralRadiance and the geometry vars. Always runs first.
/// - `Qe`: mean electrons per channel (camera equation, quantum efficiency, dark current); writes
///   Electrons.
/// - `Noise`: shot, read, PRNU and DSNU noise on Electrons.
/// - `Adc`: digital numbers from Electrons; writes RawDN.
/// - `Cie`: linear sRGB in cd/m^2 through the CIE observer; writes HdrColor.
/// - `Exposure`: the photographic output, HDR colour times the exposure scale, sRGB-encoded;
///   writes LdrColor.
enum class SensorFilter : std::uint8_t { Lens, Qe, Noise, Adc, Cie, Exposure };

inline constexpr std::array<SensorFilter, 6> kSensorFilters{
    SensorFilter::Lens, SensorFilter::Qe,  SensorFilter::Noise,
    SensorFilter::Adc,  SensorFilter::Cie, SensorFilter::Exposure};

/// The filter's name, as `ernie:filter:type` tokens spell it: "lens", "qe", "noise", "adc", "cie"
/// or "exposure".
std::string_view NameOf(SensorFilter filter);

/// The filter named `name`, if there is one.
std::optional<SensorFilter> SensorFilterNamed(std::string_view name);

/// Every filter in its natural order: the sensor chain then the photographic chain.
std::vector<SensorFilter> DefaultFilterChain();

/// `chain` with `Lens` prepended when it does not start with it.
/// @throws std::invalid_argument if a filter repeats or runs before what it reads (`Noise` and
/// `Adc` need `Qe` earlier, `Exposure` needs `Cie` earlier).
std::vector<SensorFilter> CheckFilterChain(std::span<const SensorFilter> chain);

/// The render vars `chain` writes.
std::vector<std::string_view> VarsWrittenBy(std::span<const SensorFilter> chain);

/// The images one frame renders into: the G-buffer and depth of the raster passes, the spectral
/// radiance of the shading pass, and every render var the sensor filters write. Render var images
/// stay in the general layout between frames; with `shared` set the single-layer ones live in
/// memory CUDA can map in place.
class RenderTargets {
  public:
    /// G-buffer attachment formats: normal and visibility, albedo scale and modulation, ids.
    static constexpr std::array<VkFormat, 3> kGBufferFormats{
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_UINT};
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    /// @throws std::runtime_error if allocation fails, or std::invalid_argument for a zero extent
    /// or a band count outside 1 to 64.
    RenderTargets(const VulkanContext& context, VkExtent2D extent, int bands, bool shared);

    VkExtent2D Extent() const {
        return extent_;
    }

    int Bands() const {
        return bands_;
    }

    bool Shared() const {
        return shared_;
    }

    /// Records the transitions that make the G-buffer and depth attachments, and returns them
    /// for `BeginRendering`, clearing the ids to "background".
    RenderingTarget PrepareGBuffer(VkCommandBuffer commands);

    /// Records the transitions that let compute passes read the G-buffer, depth and raster
    /// radiance and write the radiance and the render vars.
    void PrepareCompute(VkCommandBuffer commands);

    GpuImage& GBuffer(std::size_t index) {
        return *gbuffer_.at(index);
    }

    GpuImage& Depth() {
        return *depth_;
    }

    /// Spectral radiance of the raster image, before the lens filter.
    GpuImage& RasterRadiance() {
        return *rasterRadiance_;
    }

    /// The image of render var `name`, or null for an unknown name.
    GpuImage* Var(std::string_view name);

    /// Every render var name, in binding order of the filters.
    static std::span<const std::string_view> VarNames();

    std::uint64_t GpuBytes() const;

  private:
    const VulkanContext& context_;
    VkExtent2D extent_;
    int bands_;
    bool shared_;
    std::array<std::unique_ptr<GpuImage>, 3> gbuffer_;
    std::unique_ptr<GpuImage> depth_;
    std::unique_ptr<GpuImage> rasterRadiance_;
    std::vector<std::unique_ptr<GpuImage>> vars_;
};

}  // namespace ernie
