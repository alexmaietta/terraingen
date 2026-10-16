/// @file
/// @brief Heights read from a band of a GeoTIFF DEM.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <filesystem>
#include <memory>
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

    bool operator==(const GeoTiffSourceOptions&) const = default;
};

/// Heights from one band of a GeoTIFF in a projected CRS, optionally resampled to square
/// cells of `pixelSize` over the file's extent.
///
/// The file is read on first use and kept. On the source's own lattice `Fill` copies cells exactly;
/// on any other grid each cell is the file interpolated at its centre with `resampling`. Cells
/// beyond the file are no-data. Samples are scaled with the band's scale and offset, which `scale`
/// and `offset` replace; when that changes the samples, cells without data become NaN.
class GeoTiffSource final : public Source {
  public:
    /// Reads the file's header.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a geographic CRS (craters are sized in
    /// metres), a non-positive `pixelSize`, a rotated raster that would need resampling, a
    /// `scale` or `offset` that is not finite, or a negative or non-finite
    /// `lowPass`.
    explicit GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options = {});

    /// The file's grid, resampled to `pixelSize` when set.
    RasterGrid Grid() const override {
        return grid_;
    }

    /// The top-left corner of the grid, in CRS units.
    Vec2d ReferencePoint() const override;

    /// Copies cells on the source's lattice, otherwise samples the file at cell centres.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights) const override;

    /// The height in metres of the source cell centred at CRS (`x`, `y`) when there is one,
    /// otherwise of the file interpolated there with `resampling` (`SampleRaster`); NaN beyond
    /// the file and where it has no data.
    /// @throws GeoTiffError if the file cannot be read.
    float HeightAt(double x, double y) const override;

    /// The file's projection.
    GeoKeys Keys() const override {
        return info_.geoKeys;
    }

    /// The scale and offset the samples are read with.
    const BandScaling& Scaling() const {
        return scaling_;
    }

  private:
    /// Where a grid on the source's lattice starts, in source cells.
    struct LatticeOffset {
        long long col = 0;
        long long row = 0;
    };

    /// The offset of a grid with transform `target` when it lies on the source's lattice.
    std::optional<LatticeOffset> OffsetOnLattice(const GeoTransform& target) const;
    /// Copies the source cells under `heights`, which starts at `offset`.
    void CopyFromLattice(Raster<float>& heights, LatticeOffset offset) const;
    /// Sets each cell of `heights` to `HeightAt` its centre.
    void FillBySampling(Raster<float>& heights) const;
    /// Reads the file once, low-passes and resamples it as the options say.
    void Load() const;
    const Raster<float>& Heights() const;
    const Raster<float>& Native() const;

    std::filesystem::path path_;
    GeoTiffSourceOptions options_;
    GeoTiffInfo info_;
    BandScaling scaling_;
    RasterGrid grid_;
    mutable std::once_flag loaded_;
    mutable std::shared_ptr<const Raster<float>> native_;
    mutable std::shared_ptr<const Raster<float>> heights_;
};

}  // namespace terraingen
