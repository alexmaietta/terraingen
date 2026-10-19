/// @file
/// @brief Scalar fields over a surface, such as density maps and catalog completeness limits.
#pragma once

#include "terraingen/raster/raster.h"
#include "terraingen/raster/resample.h"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>

namespace terraingen {

/// A scalar quantity over a surface, read at surface coordinates: metres on a plane, longitude
/// and latitude in degrees on a sphere. Values depend only on position, so tiles, point queries
/// and threads agree; implementations must be thread-safe.
class SurfaceField {
  public:
    virtual ~SurfaceField() = default;

    /// The value at surface (x, y); NaN where the field has none.
    virtual double ValueAt(double x, double y) const = 0;

    /// The largest value the field takes anywhere, a bound for drawing features from it; NaN when
    /// it has no values.
    virtual double Max() const = 0;
};

/// The same value everywhere.
class ConstantField final : public SurfaceField {
  public:
    /// @param value Any value; NaN gives a field with none.
    explicit ConstantField(double value) : value_(value) {}

    double ValueAt(double /*x*/, double /*y*/) const override {
        return value_;
    }
    double Max() const override {
        return value_;
    }

  private:
    double value_;
};

/// How a `RasterField` reads its raster.
struct RasterFieldOptions {
    /// `Nearest`, or `Bilinear` over the valid cells around a point, weights renormalized, so a
    /// no-data cell only removes itself; both stay within the range of the cells they read.
    ResampleMethod method = ResampleMethod::Bilinear;
    /// Multiplies every cell, such as 0.001 for a map stored in thousandths; positive.
    double scale = 1.0;
    /// The value where the raster has none: outside its extent, and where every cell read is
    /// no-data or NaN. NaN leaves those points without a value.
    double fallback = std::numeric_limits<double>::quiet_NaN();
    /// The raster's x is longitude in degrees, repeating every 360: points outside its extent are
    /// read 360 degrees east or west, so a map from 0 to 360 serves -180 to 180.
    bool periodicLongitude = false;

    bool operator==(const RasterFieldOptions&) const = default;
};

/// Checks raster field options.
/// @throws std::invalid_argument for a cubic method, or a scale that is not positive and finite.
void ValidateRasterFieldOptions(const RasterFieldOptions& options);

/// A field interpolated from a raster in the surface's coordinates.
class RasterField final : public SurfaceField {
  public:
    /// @throws std::invalid_argument for an empty or rotated raster, or invalid options.
    explicit RasterField(Raster<float> raster, RasterFieldOptions options = {});

    double ValueAt(double x, double y) const override;

    /// The largest finite cell times the scale, or the fallback if larger.
    double Max() const override {
        return max_;
    }

    /// The raster read.
    const Raster<float>& Values() const {
        return raster_;
    }

    /// How it is read.
    const RasterFieldOptions& Options() const {
        return options_;
    }

  private:
    /// The raster at (x, y) before scaling: its nearest cell, or the bilinear mean of the valid
    /// cells around it; NaN outside its extent or where every cell read is invalid.
    double Sample(double x, double y) const;
    /// Whether a cell is finite and not no-data.
    bool IsValid(std::size_t col, std::size_t row) const;
    /// The cell holding `pixel` (pixel coordinates inside the raster), NaN where invalid.
    double SampleNearest(const Vec2d& pixel) const;
    /// The weighted mean of the valid cells around `pixel`, NaN where none is.
    double SampleBilinear(const Vec2d& pixel) const;

    Raster<float> raster_;
    RasterFieldOptions options_;
    double max_ = std::numeric_limits<double>::quiet_NaN();
};

/// A field read from one band of a GeoTIFF in the surface's coordinates.
/// @param band The band, from 0.
/// @throws GeoTiffError if the file cannot be read.
/// @throws std::invalid_argument as `RasterField`.
std::shared_ptr<const RasterField> ReadRasterField(const std::filesystem::path& path,
                                                   RasterFieldOptions options = {},
                                                   std::size_t band = 0);

}  // namespace terraingen
