// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `RasterAlbedo`, an albedo map upscaled with fractal detail that continues its roughness.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/layers/fade.h"
#include "terraingen/raster/resample.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// Detail below a map's resolution: fractal noise that continues the map's own measured
/// roughness to finer scales, multiplying the albedo. Octaves too fine for the sample spacing fade
/// out.
struct AlbedoDetail {
    bool enabled = true;     ///< Whether detail is added.
    std::uint32_t seed = 0;  ///< Noise seed.
    int octaves = 4;         ///< Octaves of detail; 1 to 16.
    double scale = 1.0;      ///< Multiplies the RMS of every octave; 0 adds none.
    /// The Hurst exponent to use instead of the measured one, in [0, 1].
    std::optional<double> hurst;
    ResolutionFade fade{};  ///< How octaves too fine for the sample spacing fade out.

    bool operator==(const AlbedoDetail& other) const {
        return enabled == other.enabled && seed == other.seed && octaves == other.octaves &&
               scale == other.scale && hurst == other.hurst && fade == other.fade;
    }
    bool operator!=(const AlbedoDetail& other) const {
        return !(*this == other);
    }
};

/// Checks albedo detail settings.
/// @throws std::invalid_argument naming the first field out of its range, or for an invalid fade.
void ValidateAlbedoDetail(const AlbedoDetail& detail);

/// How a `RasterAlbedo` interpolates its map and adds detail.
struct RasterAlbedoOptions {
    ResampleMethod method = ResampleMethod::Cubic;  ///< Interpolation of the map.
    AlbedoDetail detail{};                          ///< Sub-cell detail.

    bool operator==(const RasterAlbedoOptions& other) const {
        return method == other.method && detail == other.detail;
    }
    bool operator!=(const RasterAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// An albedo map, such as a normal albedo mosaic from orbit, upscaled to the grid:
/// interpolated with `method` (`SampleRaster`), then given sub-cell detail (`AlbedoDetail`).
/// Outside the map, or where it has no data, the albedo is NaN.
class RasterAlbedo final : public AlbedoSource {
  public:
    /// Builds the albedo and measures the map's roughness; on a body the map is in longitude and
    /// latitude, its cells sized on the context's surface.
    /// @param bands 1 or 3 rasters on one grid.
    /// @throws std::invalid_argument for a bad band count, mismatched rasters or invalid options.
    RasterAlbedo(std::vector<Raster<float>> bands, RasterAlbedoOptions options = {});

    /// 1 or 3 bands, as the map has.
    std::size_t Bands() const override {
        return bands_.size();
    }

    /// The map interpolated at world (`x`, `y`) (longitude and latitude on a sphere), with detail
    /// resolved at `context.spacing`; NaN outside the map or where it has no data.
    AlbedoSample ValueAt(double x, double y, const LayerContext& context) const override;

    /// The Hurst exponent the detail uses: `detail.hurst`, or the one measured.
    double Hurst() const {
        return hurst_;
    }

    /// The map's roughness at one cell, sqrt(D(c) / 2) of the logarithm of the brightness; 0 when
    /// the map is too small or too uniform to measure.
    double CellRoughness() const {
        return roughness_;
    }

    /// Interpolation and detail.
    const RasterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    /// Measures `roughness_` and `hurst_` from the map.
    void MeasureRoughness();
    /// The sum of the detail octaves at (`x`, `y`) on `surface` resolved at `spacing`.
    double DetailSum(double x, double y, double spacing, const Surface& surface) const;

    std::vector<Raster<float>> bands_;
    RasterAlbedoOptions options_;
    double hurst_ = 0.5;
    double roughness_ = 0.0;
};

}  // namespace terraingen
