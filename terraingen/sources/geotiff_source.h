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
/// The file is read the first time heights are needed and kept for later calls. `Fill` accepts any
/// grid on the source's lattice, such as a tile; cells beyond the file are no-data.
class GeoTiffSource final : public Source {
  public:
    /// Reads the file's header.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a geographic CRS (craters are sized in
    /// metres), a non-positive `pixelSize`, or a rotated raster that would need resampling.
    explicit GeoTiffSource(std::filesystem::path path, std::size_t band = 0,
                           std::optional<double> pixelSize = std::nullopt,
                           ResampleMethod resampling = ResampleMethod::Cubic);

    RasterGrid Grid() const override {
        return grid_;
    }

    /// The top-left corner of the grid.
    Vec2d ReferencePoint() const override;

    /// @throws std::invalid_argument if `heights` is not on the source's lattice.
    /// @throws GeoTiffError if the file cannot be read.
    void Fill(Raster<float>& heights) const override;

    /// The file's projection, to write outputs with.
    const GeoKeys& Keys() const {
        return info_.geoKeys;
    }

  private:
    const Raster<float>& Heights() const;

    std::filesystem::path path_;
    std::size_t band_;
    std::optional<double> pixelSize_;
    ResampleMethod resampling_;
    GeoTiffInfo info_;
    RasterGrid grid_;
    mutable std::once_flag loaded_;
    mutable std::shared_ptr<const Raster<float>> heights_;
};

}  // namespace terraingen
