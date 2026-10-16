#pragma once

#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/source.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace terraingen {

/// Heights from one band of a GeoTIFF in a projected CRS, optionally resampled to square cells of
/// `pixelSize` over the file's extent, as when a DEM is upscaled before relief is added.
///
/// The file is read the first time heights are needed and kept for later calls. On the source's own
/// lattice, such as a tile of its grid, `Fill` copies cells exactly; on any other grid, such as a
/// tile at another level of detail, each cell is the file interpolated at its centre with
/// `resampling`. Cells beyond the file are no-data.
///
/// Samples are scaled to heights with the band's scale and offset (`GeoTiffInfo::bandScaling`),
/// either of which `scale` and `offset` replace, before resampling. When that changes the samples,
/// cells without data become NaN, so that no height can be mistaken for the file's no-data value.
class GeoTiffSource final : public Source {
  public:
    /// Reads the file's header.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a geographic CRS (craters are sized in
    /// metres), a non-positive `pixelSize`, a rotated raster that would need resampling, or a
    /// `scale` or `offset` that is not finite.
    explicit GeoTiffSource(std::filesystem::path path, std::size_t band = 0,
                           std::optional<double> pixelSize = std::nullopt,
                           ResampleMethod resampling = ResampleMethod::Cubic,
                           std::optional<double> scale = std::nullopt,
                           std::optional<double> offset = std::nullopt);

    RasterGrid Grid() const override {
        return grid_;
    }

    /// The top-left corner of the grid.
    Vec2d ReferencePoint() const override;

    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights) const override;

    /// The source cell centred at (`x`, `y`) when there is one, otherwise the file interpolated
    /// there with `resampling` (`SampleRaster`); NaN beyond the file and where it has no data.
    /// @throws GeoTiffError if the file cannot be read.
    float HeightAt(double x, double y) const override;

    /// The file's projection, to write outputs with.
    const GeoKeys& Keys() const {
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
    /// Reads the file, and resamples it to `pixelSize` when set, once.
    void Load() const;
    const Raster<float>& Heights() const;
    const Raster<float>& Native() const;

    std::filesystem::path path_;
    std::size_t band_;
    std::optional<double> pixelSize_;
    ResampleMethod resampling_;
    GeoTiffInfo info_;
    BandScaling scaling_;
    RasterGrid grid_;
    mutable std::once_flag loaded_;
    mutable std::shared_ptr<const Raster<float>> native_;
    mutable std::shared_ptr<const Raster<float>> heights_;
};

}  // namespace terraingen
