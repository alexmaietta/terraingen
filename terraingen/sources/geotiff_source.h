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

/// @brief Heights from one band of a GeoTIFF in a projected CRS, optionally resampled to square
/// cells of `pixelSize` over the file's extent.
///
/// The file is read on first use and kept. On the source's own lattice `Fill` copies cells exactly;
/// on any other grid each cell is the file interpolated at its centre with `resampling`. Cells
/// beyond the file are no-data. Samples are scaled with the band's scale and offset, which `scale`
/// and `offset` replace; when that changes the samples, cells without data become NaN.
class GeoTiffSource final : public Source {
  public:
    /// @brief Reads the file's header.
    /// @param path The GeoTIFF.
    /// @param band Zero-based band index.
    /// @param pixelSize Cell size to resample to, in CRS units; unset keeps the file's cells.
    /// @param resampling Interpolation for resampling and for cells off the lattice.
    /// @param scale Replaces the band's scale when set; finite.
    /// @param offset Replaces the band's offset when set; finite.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a geographic CRS (craters are sized in
    /// metres), a non-positive `pixelSize`, a rotated raster that would need resampling, or a
    /// `scale` or `offset` that is not finite.
    explicit GeoTiffSource(std::filesystem::path path, std::size_t band = 0,
                           std::optional<double> pixelSize = std::nullopt,
                           ResampleMethod resampling = ResampleMethod::Cubic,
                           std::optional<double> scale = std::nullopt,
                           std::optional<double> offset = std::nullopt);

    /// @brief The file's grid, resampled to `pixelSize` when set.
    /// @return The grid.
    RasterGrid Grid() const override {
        return grid_;
    }

    /// @brief The top-left corner of the grid.
    /// @return The corner in CRS units.
    Vec2d ReferencePoint() const override;

    /// @brief Copies cells on the source's lattice, otherwise samples the file at cell centres.
    /// @param heights Raster to fill.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights) const override;

    /// @brief The source cell centred at (`x`, `y`) when there is one, otherwise the file
    /// interpolated there with `resampling` (`SampleRaster`).
    /// @param x CRS x.
    /// @param y CRS y.
    /// @return Height in metres; NaN beyond the file and where it has no data.
    /// @throws GeoTiffError if the file cannot be read.
    float HeightAt(double x, double y) const override;

    /// The file's projection.
    GeoKeys Keys() const override {
        return info_.geoKeys;
    }

    /// @brief The scale and offset the samples are read with.
    /// @return The band scaling.
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
