/// @file
/// @brief Heights read from a band of a GeoTIFF DEM.
#include "terraingen/sources/geotiff_source.h"

#include "terraingen/util/checks.h"

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
                             std::optional<double> pixelSize, ResampleMethod resampling,
                             std::optional<double> scale, std::optional<double> offset)
    : path_(std::move(path)), band_(band), pixelSize_(pixelSize), resampling_(resampling),
      info_(ReadGeoTiffInfo(path_)) {
    if (band_ >= info_.bands) {
        throw std::invalid_argument("Band " + std::to_string(band_) + " is missing; the file has " +
                                    std::to_string(info_.bands) +
                                    (info_.bands == 1 ? " band" : " bands"));
    }
    if ((scale && !std::isfinite(*scale)) || (offset && !std::isfinite(*offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
    scaling_ = {scale.value_or(info_.bandScaling[band_].scale),
                offset.value_or(info_.bandScaling[band_].offset)};
    if (info_.geoKeys.IsGeographic()) {
        throw std::invalid_argument(
            "The raster's CRS is geographic (degrees); craters are sized in metres, so reproject "
            "it first, for example with gdalwarp -t_srs");
    }
    grid_ = {info_.width, info_.height, info_.transform};
    if (pixelSize_) {
        grid_ = ResampledGrid(grid_, *pixelSize_);
    }
}

Vec2d GeoTiffSource::ReferencePoint() const {
    return {grid_.transform.originX, grid_.transform.originY};
}

void GeoTiffSource::Load() const {
    std::call_once(loaded_, [this] {
        GeoTiff tiff = ReadGeoTiff(path_, band_);
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
        native_ = std::make_shared<const Raster<float>>(std::move(tiff.raster));
        heights_ = pixelSize_ ? std::make_shared<const Raster<float>>(
                                    Resample(*native_, *pixelSize_, resampling_))
                              : native_;
    });
}

const Raster<float>& GeoTiffSource::Heights() const {
    Load();
    return *heights_;
}

const Raster<float>& GeoTiffSource::Native() const {
    Load();
    return *native_;
}

std::optional<GeoTiffSource::LatticeOffset>
GeoTiffSource::OffsetOnLattice(const GeoTransform& target) const {
    const GeoTransform& source = grid_.transform;
    const auto same = [](double a, double b) { return std::fabs(a - b) <= 1e-12 * std::fabs(b); };
    const std::optional<long long> col =
        Whole((target.originX - source.originX) / source.pixelWidth);
    const std::optional<long long> row =
        Whole((target.originY - source.originY) / source.pixelHeight);
    if (!same(target.pixelWidth, source.pixelWidth) ||
        !same(target.pixelHeight, source.pixelHeight) || target.rowRotation != source.rowRotation ||
        target.colRotation != source.colRotation || !col || !row) {
        return std::nullopt;
    }
    return LatticeOffset{*col, *row};
}

void GeoTiffSource::CopyFromLattice(Raster<float>& heights, LatticeOffset offset) const {
    const Raster<float>& data = Heights();
    const float fill = data.NoData().value_or(std::numeric_limits<float>::quiet_NaN());
    bool outside = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        const long long sourceRow = offset.row + static_cast<long long>(row);
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const long long sourceCol = offset.col + static_cast<long long>(col);
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
    heights.SetNoData(FilledNoData(data.NoData(), outside));
}

void GeoTiffSource::FillBySampling(Raster<float>& heights) const {
    const GeoTransform& target = heights.Transform();
    if (target.rowRotation != 0.0 || target.colRotation != 0.0) {
        throw std::invalid_argument("A rotated grid off the lattice of " + path_.string() +
                                    " cannot be filled");
    }
    SampleAtCentres(heights, Heights().NoData(),
                    [this](double x, double y) { return HeightAt(x, y); });
}

void GeoTiffSource::Fill(Raster<float>& heights) const {
    if (const auto offset = OffsetOnLattice(heights.Transform())) {
        CopyFromLattice(heights, *offset);
    } else {
        FillBySampling(heights);
    }
}

float GeoTiffSource::HeightAt(double x, double y) const {
    const Raster<float>& data = Heights();
    const Vec2d pixel = grid_.transform.WorldToPixel(x, y);
    const double col = std::floor(pixel.x);
    const double row = std::floor(pixel.y);
    const bool centred =
        std::fabs(pixel.x - col - 0.5) < 1e-9 && std::fabs(pixel.y - row - 0.5) < 1e-9;
    if (!centred) {
        return SampleRaster(Native(), x, y, resampling_);
    }
    if (!(col >= 0.0 && row >= 0.0 && col < static_cast<double>(data.Width()) &&
          row < static_cast<double>(data.Height()))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const float value = data(static_cast<std::size_t>(col), static_cast<std::size_t>(row));
    return data.IsNoData(value) ? std::numeric_limits<float>::quiet_NaN() : value;
}

}  // namespace terraingen
