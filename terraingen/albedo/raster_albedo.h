/// @file
/// @brief `RasterAlbedo`, an albedo map upscaled with fractal detail that continues its roughness.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/fade.h"
#include "terraingen/raster/resample.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// Detail below a map's resolution: fractal noise that continues the map's own roughness to finer
/// scales. The map's roughness is measured once from the structure function of the logarithm of
/// its brightness at lags of 1, 2, 4 and 8 cells; octave i of the detail has wavelength c / 2^(i+1)
/// for cell size c and RMS sqrt(D(c) / 2) 2^(-(i+1)H) for Hurst exponent H, and the albedo is
/// multiplied by e to their sum. Octaves too fine for the sample spacing fade out
/// (`ResolutionFade`). See SCIENCE.md, "Albedo detail".
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

/// @brief Checks albedo detail settings.
/// @param detail The settings to check.
/// @throws std::invalid_argument for octaves outside 1 to 16, a negative or non-finite scale, a
/// Hurst exponent outside [0, 1], or an invalid fade.
void ValidateAlbedoDetail(const AlbedoDetail& detail);

/// How a `RasterAlbedo` interpolates its map and adds detail.
struct RasterAlbedoOptions {
    ResampleMethod method = ResampleMethod::Cubic;
    AlbedoDetail detail{};

    bool operator==(const RasterAlbedoOptions&) const = default;
};

/// @brief The logarithm of the mean of a map's bands at each cell.
/// @param bands One or more rasters on one grid.
/// @return Row-major values; NaN where a band has no data or is not positive.
std::vector<double> LogBrightnessGrid(const std::vector<Raster<float>>& bands);

/// @brief The structure function of a grid at a lag: the mean squared difference of the values a
/// lag apart along rows and columns.
/// @param values Row-major values; non-finite values are skipped.
/// @param width Columns of the grid.
/// @param height Rows of the grid.
/// @param lag The lag, in cells.
/// @return The mean, or nothing when no pair was found or every pair is equal.
std::optional<double> StructureFunction(const std::vector<double>& values, std::size_t width,
                                        std::size_t height, std::size_t lag);

/// @brief The least-squares slope of a line through points.
/// @param x The abscissae; at least two, not all equal.
/// @param y The ordinates, one per abscissa.
/// @return The slope.
double FitSlope(const std::vector<double>& x, const std::vector<double>& y);

/// An albedo map, such as a normal albedo mosaic from orbit, upscaled to the grid: interpolated
/// with `method` (`SampleRaster`), then given sub-cell detail (`AlbedoDetail`). Outside the map,
/// or where it has no data, the albedo is NaN.
class RasterAlbedo final : public AlbedoSource {
  public:
    /// @brief Builds the albedo and measures the map's roughness.
    ///
    /// On a sphere (`surface`) the map is in longitude and latitude, its cell size is its latitude
    /// step in metres, and the detail is 3D Perlin noise at each point's 3D position.
    /// @param bands 1 or 3 rasters on one grid.
    /// @param options Interpolation and detail.
    /// @param surface What the map lies on.
    /// @throws std::invalid_argument for another band count, empty, rotated or mismatched rasters,
    /// or invalid options.
    RasterAlbedo(std::vector<Raster<float>> bands, RasterAlbedoOptions options = {},
                 Surface surface = {});

    std::size_t Bands() const override {
        return bands_.size();
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override;

    /// @brief The Hurst exponent the detail uses.
    /// @return `detail.hurst`, or the one measured.
    double Hurst() const {
        return hurst_;
    }

    /// @brief The map's roughness at one cell.
    /// @return sqrt(D(c) / 2) of the logarithm of the brightness, as measured; 0 when the map is
    /// too small or too uniform to measure.
    double CellRoughness() const {
        return roughness_;
    }

    const RasterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    /// Measures `roughness_` and `hurst_` from the map.
    void MeasureRoughness();
    /// The sum of the detail octaves at (`x`, `y`) resolved at `spacing`.
    double DetailSum(double x, double y, double spacing) const;

    std::vector<Raster<float>> bands_;
    RasterAlbedoOptions options_;
    Surface surface_;
    double cell_ = 0.0;
    double hurst_ = 0.5;
    double roughness_ = 0.0;
};

}  // namespace terraingen
