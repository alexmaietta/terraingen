#include "terraingen/sources/geotiff_source.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// `value` as an integer when it is one to within rounding, as offsets between lattices are.
std::optional<long long> Whole(double value) {
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) > 1e-6) {
        return std::nullopt;
    }
    return static_cast<long long>(rounded);
}

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, std::size_t band,
                             std::optional<double> pixelSize, ResampleMethod resampling)
    : path_(std::move(path)), band_(band), pixelSize_(pixelSize), resampling_(resampling),
      info_(ReadGeoTiffInfo(path_)) {
    if (band_ >= info_.bands) {
        throw std::invalid_argument("Band " + std::to_string(band_) + " is missing; the file has " +
                                    std::to_string(info_.bands) +
                                    (info_.bands == 1 ? " band" : " bands"));
    }
    if (info_.geoKeys.IsGeographic()) {
        throw std::invalid_argument(
            "The raster's CRS is geographic (degrees); craters are sized in metres, so reproject "
            "it first, for example with gdalwarp -t_srs");
    }
    grid_ = {info_.width, info_.height, info_.transform};
    if (pixelSize_) {
        if (!(std::isfinite(*pixelSize_) && *pixelSize_ > 0.0)) {
            throw std::invalid_argument("Pixel size must be positive and finite");
        }
        GeoTransform& t = grid_.transform;
        if (t.rowRotation != 0.0 || t.colRotation != 0.0) {
            throw std::invalid_argument("Rotated rasters cannot be resampled");
        }
        grid_.width = static_cast<std::size_t>(std::max(
            1.0,
            std::round(static_cast<double>(info_.width) * std::fabs(t.pixelWidth) / *pixelSize_)));
        grid_.height = static_cast<std::size_t>(
            std::max(1.0, std::round(static_cast<double>(info_.height) * std::fabs(t.pixelHeight) /
                                     *pixelSize_)));
        t.pixelWidth = std::copysign(*pixelSize_, t.pixelWidth);
        t.pixelHeight = std::copysign(*pixelSize_, t.pixelHeight);
    }
}

Vec2d GeoTiffSource::ReferencePoint() const {
    return {grid_.transform.originX, grid_.transform.originY};
}

const Raster<float>& GeoTiffSource::Heights() const {
    std::call_once(loaded_, [this] {
        GeoTiff tiff = ReadGeoTiff(path_, band_);
        heights_ = std::make_shared<const Raster<float>>(
            pixelSize_ ? Resample(tiff.raster, *pixelSize_, resampling_) : std::move(tiff.raster));
    });
    return *heights_;
}

void GeoTiffSource::Fill(Raster<float>& heights) const {
    const GeoTransform& source = grid_.transform;
    const GeoTransform& target = heights.Transform();
    const std::optional<long long> colOffset =
        Whole((target.originX - source.originX) / source.pixelWidth);
    const std::optional<long long> rowOffset =
        Whole((target.originY - source.originY) / source.pixelHeight);
    if (target.pixelWidth != source.pixelWidth || target.pixelHeight != source.pixelHeight ||
        target.rowRotation != source.rowRotation || target.colRotation != source.colRotation ||
        !colOffset || !rowOffset) {
        throw std::invalid_argument("The grid is not on the lattice of " + path_.string());
    }
    const Raster<float>& data = Heights();
    std::optional<float> noData = data.NoData();
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool outside = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        const long long sourceRow = *rowOffset + static_cast<long long>(row);
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const long long sourceCol = *colOffset + static_cast<long long>(col);
            if (sourceRow < 0 || sourceCol < 0 ||
                sourceRow >= static_cast<long long>(data.Height()) ||
                sourceCol >= static_cast<long long>(data.Width())) {
                heights(col, row) = fill;
                outside = true;
                continue;
            }
            heights(col, row) =
                data(static_cast<std::size_t>(sourceCol), static_cast<std::size_t>(sourceRow));
        }
    }
    heights.SetNoData(noData ? noData : (outside ? std::optional<float>(fill) : std::nullopt));
}

}  // namespace terraingen
