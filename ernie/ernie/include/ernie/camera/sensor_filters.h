#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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

/// Every render var name, in the order the rasterizer binds their images.
inline constexpr std::array<std::string_view, 10> kRenderVarNames{
    render_vars::kSpectralRadiance, render_vars::kElectrons, render_vars::kRawDn,
    render_vars::kHdrColor,         render_vars::kLdrColor,  render_vars::kDistanceToImagePlane,
    render_vars::kDistanceToCamera, render_vars::kNormal,    render_vars::kPrimId,
    render_vars::kInstanceId};

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

}  // namespace ernie
