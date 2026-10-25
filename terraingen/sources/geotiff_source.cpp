// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/geotiff_source.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// How the band's cells become heights.
RasterSourceOptions SamplingOptions(const GeoTiffSourceOptions& options) {
    RasterSourceOptions sampling;
    sampling.effectivePixelSize = options.effectivePixelSize;
    sampling.upsampler = options.upsampler;
    return sampling;
}

/// The cells of `lattice` that meet `crop`.
/// @throws std::invalid_argument for a crop that is not finite, is empty or misses the lattice.
PixelWindow CropCells(const RasterGrid& lattice, const Bounds2d& crop) {
    if (!crop.IsFinite()) {
        throw std::invalid_argument("The crop must be finite");
    }
    if (!(crop.minX < crop.maxX && crop.minY < crop.maxY)) {
        throw std::invalid_argument("The crop must have minX < maxX and minY < maxY");
    }
    const PixelWindow cells = PixelWindowIn(lattice, crop);
    if (cells.Empty()) {
        throw std::invalid_argument("The crop misses the file");
    }
    return cells;
}

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options)
    : path_(std::move(path)), options_(std::move(options)), info_(ReadGeoTiffInfo(path_)) {
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
    detail::ValidateRasterSourceOptions(SamplingOptions(options_));
    scaling_ = {scale.value_or(info_.bandScaling[band].scale),
                offset.value_or(info_.bandScaling[band].offset)};
    lattice_ = {info_.width, info_.height, info_.transform};
    cells_ = options_.crop ? CropCells(lattice_, *options_.crop) : WholeWindow(lattice_);
}

const detail::RasterHeights& GeoTiffSource::Heights() const {
    std::call_once(loaded_, [this] {
        const RasterSourceOptions sampling = SamplingOptions(options_);
        const bool wraps = detail::WrapsLongitude(info_.geoKeys, lattice_);
        PixelWindow window =
            detail::NativeWindow(lattice_, detail::OptionUpsampler(sampling)->Reach(), cells_);
        if (wraps) {
            window.colBegin = 0;
            window.colEnd = lattice_.width;
        }
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
                         RasterPlacement{lattice_, window.colBegin, window.rowBegin}, sampling,
                         cells_, path_.string(), wraps);
    });
    return *heights_;
}

void GeoTiffSource::Fill(Raster<float>& heights, double spacing) const {
    Heights().Fill(heights, spacing);
}

float GeoTiffSource::HeightAt(double x, double y, double spacing) const {
    return Heights().HeightAt(x, y, spacing);
}

double GeoTiffSource::ResolutionAt(double x, double y) const {
    return Heights().ResolutionAt(x, y);
}

}  // namespace terraingen
