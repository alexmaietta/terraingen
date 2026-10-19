/// @file
/// @brief Heights read from a band of a GeoTIFF DEM.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>

namespace terraingen {

/// How a `GeoTiffSource` reads its band.
struct GeoTiffSourceOptions {
    std::size_t band = 0;  ///< Zero-based band index.
    /// Cell size to resample to, in CRS units; unset keeps the file's cells.
    std::optional<double> pixelSize{};
    /// Interpolation for resampling and for cells off the source's lattice.
    ResampleMethod resampling = ResampleMethod::Cubic;
    std::optional<double> scale{};   ///< Replaces the band's scale when set; finite.
    std::optional<double> offset{};  ///< Replaces the band's offset when set; finite.
    /// Standard deviation in metres of a Gaussian low-pass applied to the file's heights once,
    /// before any resampling: the low frequencies of a DEM to add finer synthetic detail to
    /// (`DetailLayer` can add the removed detail back). 0 keeps every frequency.
    double lowPass = 0.0;
    /// The box in CRS units the source covers: only the cells of its grid that meet it, read
    /// from a window of the file. Unset covers the whole file.
    std::optional<Bounds2d> region{};

    /// Memberwise equality.
    bool operator==(const GeoTiffSourceOptions& other) const {
        return band == other.band && pixelSize == other.pixelSize &&
               resampling == other.resampling && scale == other.scale && offset == other.offset &&
               lowPass == other.lowPass && region == other.region;
    }
    bool operator!=(const GeoTiffSourceOptions& other) const {
        return !(*this == other);
    }
};

/// Heights from one band of a GeoTIFF in a projected CRS, optionally resampled to square
/// cells of `pixelSize` over the file's extent: a `RasterSource` of the file's band.
///
/// The cells the source needs are read on first use and kept: the whole file, or with a `region`
/// a window of it, the cells under the region's grid and enough around them for the low-pass and
/// the interpolation, so that the source gives the same bits there as without the region. On the
/// source's own lattice `Fill` copies cells exactly; on any other grid each cell is the file
/// interpolated at its centre with `resampling`. Cells beyond the file, or beyond the region's
/// grid, are no-data. Samples are scaled with the band's scale and offset, which `scale` and
/// `offset` replace; when that changes the samples, cells without data become NaN.
class GeoTiffSource final : public Source {
  public:
    /// Reads the file's header.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a geographic CRS (craters are sized in
    /// metres), a non-positive `pixelSize`, a rotated raster that would need resampling, a
    /// `scale` or `offset` that is not finite, a negative or non-finite `lowPass`, or a `region`
    /// that is not finite, is empty (`minX` >= `maxX` or `minY` >= `maxY`) or misses the file.
    explicit GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options = {});

    /// The file's grid, resampled to `pixelSize` when set, and clipped to the cells that meet
    /// `region` when set.
    RasterGrid Grid() const override {
        return Subgrid(lattice_, cells_);
    }

    /// The top-left corner of the file's grid, in CRS units, with or without a region.
    Vec2d ReferencePoint() const override;

    /// Copies cells on the source's lattice, otherwise samples the file at cell centres.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights) const override;

    /// The height in metres of the source cell centred at CRS (`x`, `y`) when there is one,
    /// otherwise of the file interpolated there with `resampling` (`SampleRaster`); NaN beyond
    /// the file or the region's grid, and where it has no data.
    /// @throws GeoTiffError if the file cannot be read.
    float HeightAt(double x, double y) const override;

    /// The file's cell size in metres, the larger side of a cell, or twice `lowPass` when that is
    /// larger; infinity where `HeightAt` is NaN.
    /// @throws GeoTiffError if the file cannot be read.
    double ResolutionAt(double x, double y) const override;

    /// The file's projection.
    GeoKeys Keys() const override {
        return info_.geoKeys;
    }

    /// The scale and offset the samples are read with.
    const BandScaling& Scaling() const {
        return scaling_;
    }

  private:
    /// Reads the cells needed once, scales, low-passes and resamples them as the options say.
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
