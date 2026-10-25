// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Heights read from a band of a GeoTIFF DEM.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/upsampling.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace terraingen {

/// How a `GeoTiffSource` reads its band.
struct GeoTiffSourceOptions {
    std::size_t band = 0;            ///< Zero-based band index.
    std::optional<double> scale{};   ///< Replaces the band's scale when set; finite.
    std::optional<double> offset{};  ///< Replaces the band's offset when set; finite.
    /// The box in CRS units the source covers: only the cells of its grid that meet it, read
    /// from a window of the file. Unset covers the whole file.
    std::optional<Bounds2d> crop{};
    /// The cell size in meters the file's heights resolve, for a DEM that was upsampled or
    /// smoothed before it was saved: `ResolutionAt` gives it in place of the cells' size. Unset
    /// uses the cells'.
    std::optional<double> effectivePixelSize{};
    /// How the cells become heights between them and at sample spacings finer than the cells;
    /// null for cubic interpolation (`DefaultUpsampler`).
    std::shared_ptr<const Upsampler> upsampler{};

    /// Memberwise equality, the upsamplers compared by identity.
    bool operator==(const GeoTiffSourceOptions& other) const {
        return band == other.band && scale == other.scale && offset == other.offset &&
               crop == other.crop && effectivePixelSize == other.effectivePixelSize &&
               upsampler == other.upsampler;
    }
    bool operator!=(const GeoTiffSourceOptions& other) const {
        return !(*this == other);
    }
};

/// Heights from one band of a GeoTIFF in a projected CRS, or in longitude and latitude (a
/// geographic CRS) for a terrain on a body, read on first use (only a window around `crop` when
/// set, giving the same bits as the whole file) and served as a `RasterSource` serves its raster;
/// samples take the band's scale and offset, or `scale` and `offset`. A file in longitude and
/// latitude whose columns span 360 degrees wraps in longitude as a `RasterSource` does, and then
/// reads every column of the rows a crop meets.
class GeoTiffSource final : public Source {
  public:
    using Source::Fill;

    /// Reads the file's header.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a `scale` or `offset` that is not finite,
    /// an `effectivePixelSize` that is not positive and finite, or a `crop` that is not finite, is
    /// empty (`minX` >= `maxX` or `minY` >= `maxY`) or misses the file.
    explicit GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options = {});

    /// The file's grid, clipped to the cells that meet `crop` when set.
    RasterGrid Grid() const override {
        return Subgrid(lattice_, cells_);
    }

    /// The top-left corner of the file's grid, in CRS units, with or without a crop.
    Vec2d ReferencePoint() const override {
        return detail::Corner(lattice_);
    }

    /// Copies cells on the source's lattice at its cell size or coarser, otherwise upsamples.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights, double spacing) const override;

    /// The height in meters of the source cell centered at CRS (`x`, `y`) at the cell size or
    /// coarser, otherwise the upsampler's; NaN beyond the file or the crop's grid, and where it
    /// has no data.
    /// @throws GeoTiffError if the file cannot be read.
    float HeightAt(double x, double y, double spacing) const override;

    /// `effectivePixelSize`, or else the file's cell size in CRS units (degrees when geographic),
    /// the larger side of a cell; infinity where `HeightAt` is NaN.
    /// @throws GeoTiffError if the file cannot be read.
    double ResolutionAt(double x, double y) const override;

    /// The file's projection.
    GeoKeys Keys() const override {
        return info_.geoKeys;
    }

  private:
    /// Reads the cells needed once and scales them.
    const detail::RasterHeights& Heights() const;

    std::filesystem::path path_;
    GeoTiffSourceOptions options_;
    GeoTiffInfo info_;
    BandScaling scaling_;
    RasterGrid lattice_;
    PixelWindow cells_;
    mutable std::once_flag loaded_;
    mutable std::optional<detail::RasterHeights> heights_;
};

}  // namespace terraingen
