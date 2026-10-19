#include "terraingen/sources/geotiff_source.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// How the band's heights are low-passed and resampled.
RasterSourceOptions SamplingOptions(const GeoTiffSourceOptions& options) {
    RasterSourceOptions sampling;
    sampling.pixelSize = options.pixelSize;
    sampling.resampling = options.resampling;
    sampling.lowPass = options.lowPass;
    return sampling;
}

/// The cells of `lattice` that meet `region`.
/// @throws std::invalid_argument for a region that is not finite, is empty or misses the lattice.
PixelWindow RegionCells(const RasterGrid& lattice, const Bounds2d& region) {
    if (!(std::isfinite(region.minX) && std::isfinite(region.minY) && std::isfinite(region.maxX) &&
          std::isfinite(region.maxY))) {
        throw std::invalid_argument("The region must be finite");
    }
    if (!(region.minX < region.maxX && region.minY < region.maxY)) {
        throw std::invalid_argument("The region must have minX < maxX and minY < maxY");
    }
    const PixelWindow cells = PixelWindowIn(lattice, region);
    if (cells.Empty()) {
        throw std::invalid_argument("The region misses the file");
    }
    return cells;
}

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options)
    : path_(std::move(path)), options_(options), info_(ReadGeoTiffInfo(path_)) {
    const std::size_t band = options_.band;
    if (band >= info_.bands) {
        throw std::invalid_argument("Band " + std::to_string(band) + " is missing; the file has " +
                                    std::to_string(info_.bands) +
                                    (info_.bands == 1 ? " band" : " bands"));
    }
    const std::optional<double>& scale = options_.scale;
    const std::optional<double>& offset = options_.offset;
    if ((scale && !std::isfinite(*scale)) || (offset && !std::isfinite(*offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
    RequireNonNegative(options_.lowPass, "Low-pass deviation");
    scaling_ = {scale.value_or(info_.bandScaling[band].scale),
                offset.value_or(info_.bandScaling[band].offset)};
    if (info_.geoKeys.IsGeographic()) {
        throw std::invalid_argument(
            "The raster's CRS is geographic (degrees); craters are sized in metres, so reproject "
            "it first, for example with gdalwarp -t_srs");
    }
    lattice_ = detail::SourceLattice({info_.width, info_.height, info_.transform},
                                     SamplingOptions(options_));
    cells_ = options_.region ? RegionCells(lattice_, *options_.region) : WholeWindow(lattice_);
}

Vec2d GeoTiffSource::ReferencePoint() const {
    return {lattice_.transform.originX, lattice_.transform.originY};
}

const detail::RasterHeights& GeoTiffSource::Heights() const {
    std::call_once(loaded_, [this] {
        const RasterSourceOptions sampling = SamplingOptions(options_);
        const RasterGrid native{info_.width, info_.height, info_.transform};
        const PixelWindow window = detail::NativeWindow(native, sampling, cells_);
        GeoTiff tiff = ReadGeoTiffWindow(path_, options_.band, window);
        if (!scaling_.Identity()) {
            Raster<float>& raster = tiff.raster;
            constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
            for (float& value : raster.Values()) {
                value = raster.IsNoData(value)
                            ? kMissing
                            : static_cast<float>(static_cast<double>(value) * scaling_.scale +
                                                 scaling_.offset);
            }
            if (raster.NoData()) {
                raster.SetNoData(kMissing);
            }
        }
        heights_.emplace(std::make_shared<const Raster<float>>(std::move(tiff.raster)),
                         RasterPlacement{native, window.colBegin, window.rowBegin}, sampling,
                         cells_, path_.string());
    });
    return *heights_;
}

void GeoTiffSource::Fill(Raster<float>& heights) const {
    Heights().Fill(heights);
}

float GeoTiffSource::HeightAt(double x, double y) const {
    return Heights().HeightAt(x, y);
}

double GeoTiffSource::ResolutionAt(double x, double y) const {
    return Heights().ResolutionAt(x, y);
}

}  // namespace terraingen
