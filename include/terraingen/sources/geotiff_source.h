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

/// The bytes of decoded tiles or strips a `GeoTiffSource` keeps by default: 512 MiB.
inline constexpr std::size_t kDefaultGeoTiffCacheBytes = std::size_t{512} << 20;

/// How a `GeoTiffSource` reads its band.
struct GeoTiffSourceOptions {
    std::size_t band = 0;            ///< Zero-based band index.
    std::optional<double> scale{};   ///< Replaces the band's scale when set; finite.
    std::optional<double> offset{};  ///< Replaces the band's offset when set; finite.
    /// The box in CRS units the source covers: only the cells of its grid that meet it. Unset
    /// covers the whole file.
    std::optional<Bounds2d> crop{};
    /// The cell size in meters the file's heights resolve, for a DEM that was upsampled or
    /// smoothed before it was saved: `ResolutionAt` gives it in place of the cells' size. Unset
    /// uses the cells'.
    std::optional<double> effectivePixelSize{};
    /// How the cells become heights between them and at sample spacings finer than the cells;
    /// null for cubic interpolation (`DefaultUpsampler`).
    std::shared_ptr<const Upsampler> upsampler{};
    /// The most bytes of decoded tiles or strips kept for reuse; the one read last is kept
    /// whatever the budget.
    std::size_t cacheBytes = kDefaultGeoTiffCacheBytes;

    /// Memberwise equality, the upsamplers compared by identity.
    bool operator==(const GeoTiffSourceOptions& other) const {
        return band == other.band && scale == other.scale && offset == other.offset &&
               crop == other.crop && effectivePixelSize == other.effectivePixelSize &&
               upsampler == other.upsampler && cacheBytes == other.cacheBytes;
    }
    bool operator!=(const GeoTiffSourceOptions& other) const {
        return !(*this == other);
    }
};

/// Heights from one band of a GeoTIFF in a projected CRS, or in longitude and latitude (a
/// geographic CRS) for a terrain on a body, served as a `RasterSource` serves its raster; samples
/// take the band's scale and offset, or `scale` and `offset`. The file is decoded a tile or strip
/// at a time as heights are asked of them and kept in a cache of `cacheBytes`, so a small area of
/// a file too large to hold in memory reads only the blocks around it; a `crop` gives the same
/// bits over its grid as the whole file. A file in longitude and latitude whose columns span 360
/// degrees wraps in longitude as a `RasterSource` does.
class GeoTiffSource final : public detail::RasterHeightsSource {
  public:
    /// Reads the file's header. The grid is the file's, clipped to the cells that meet `crop`
    /// when set, and the projection the file's.
    /// @throws GeoTiffError if the file cannot be read.
    /// @throws std::invalid_argument for a missing band, a `scale` or `offset` that is not finite,
    /// an `effectivePixelSize` that is not positive and finite, or a `crop` that is not finite, is
    /// empty (`minX` >= `maxX` or `minY` >= `maxY`) or misses the file.
    explicit GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options = {});

  protected:
    /// Opens the file for reading blocks on first use.
    /// @throws GeoTiffError if the file cannot be read.
    const detail::RasterHeights& Heights() const override;

  private:
    /// The source of `path` with the header `info` read from it.
    GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options, GeoTiffInfo info);

    std::filesystem::path path_;
    GeoTiffSourceOptions options_;
    GeoTiffInfo info_;
    BandScaling scaling_;
    mutable std::once_flag loaded_;
    mutable std::optional<detail::RasterHeights> heights_;
};

}  // namespace terraingen
