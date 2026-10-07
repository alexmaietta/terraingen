#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/raster/resample.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// Detail below a map's resolution: fractal noise that continues the map's own roughness to finer
/// scales.
///
/// The map's roughness is measured once, on the whole map, from the structure function of the
/// logarithm of its brightness (the mean of its bands), D(l) = <(ln a(p + l) - ln a(p))^2>, at lags
/// of 1, 2, 4 and 8 cells along rows and columns. For a fractal D(l) grows as l^(2H), so a least
/// squares line through log D against log l gives the Hurst exponent H (clamped to [0, 1]). Octave
/// i of the detail has wavelength c / 2^(i+1) for cell size c and RMS sqrt(D(c) / 2) 2^(-(i+1)H),
/// carrying the measured roughness one octave down at a time; the albedo is multiplied by e to the
/// sum, so it stays positive and the bands keep their ratios. Octaves too fine for the sample
/// spacing fade out as features do (`ResolutionFade`).
struct AlbedoDetail {
    bool enabled = true;
    std::uint32_t seed = 0;
    int octaves = 4;     ///< Octaves of detail; 1 to 16.
    double scale = 1.0;  ///< Multiplies the RMS of every octave; 0 adds none.
    /// The Hurst exponent to use instead of the measured one, in [0, 1].
    std::optional<double> hurst;
    ResolutionFade fade{};

    bool operator==(const AlbedoDetail&) const = default;
};

/// @throws std::invalid_argument for octaves outside 1 to 16, a negative or non-finite scale, a
/// Hurst exponent outside [0, 1], or an invalid fade.
void ValidateAlbedoDetail(const AlbedoDetail& detail);

/// Settings for a `RasterAlbedo`.
struct RasterAlbedoOptions {
    ResampleMethod method = ResampleMethod::Cubic;
    AlbedoDetail detail{};

    bool operator==(const RasterAlbedoOptions&) const = default;
};

/// An albedo map, such as a normal albedo mosaic from orbit, upscaled to the grid: interpolated
/// with `method` (`SampleRaster`), then given sub-cell detail (`AlbedoDetail`). Outside the map,
/// or where it has no data, the albedo is NaN.
class RasterAlbedo final : public AlbedoSource {
  public:
    /// `bands` are 1 or 3 rasters on one grid. On a sphere (`surface`) the map is in longitude and
    /// latitude, its cell size is its latitude step in metres, and the detail is 3D Perlin noise at
    /// each point's 3D position.
    /// @throws std::invalid_argument for another band count, empty, rotated or mismatched rasters,
    /// or invalid options.
    RasterAlbedo(std::vector<Raster<float>> bands, RasterAlbedoOptions options = {},
                 Surface surface = {});

    std::size_t Bands() const override {
        return bands_.size();
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override;

    /// The Hurst exponent the detail uses: `detail.hurst`, or the one measured.
    double Hurst() const {
        return hurst_;
    }

    /// sqrt(D(c) / 2) of the logarithm of the brightness at one cell, as measured; 0 when the map
    /// is too small or too uniform to measure.
    double CellRoughness() const {
        return roughness_;
    }

    const RasterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    std::vector<Raster<float>> bands_;
    RasterAlbedoOptions options_;
    Surface surface_;
    double cell_ = 0.0;
    double hurst_ = 0.5;
    double roughness_ = 0.0;
};

}  // namespace terraingen
