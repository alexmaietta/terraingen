#include "terraingen/raster/resample.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace terraingen {

namespace {

/// Keys cubic convolution kernel with a = -0.5.
double CubicWeight(double x) {
    constexpr double a = -0.5;
    x = std::fabs(x);
    if (x <= 1.0) {
        return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
    }
    if (x < 2.0) {
        return ((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a;
    }
    return 0.0;
}

std::size_t Clamp(double index, std::size_t size) {
    return static_cast<std::size_t>(std::clamp(index, 0.0, static_cast<double>(size - 1)));
}

/// `raster` interpolated at pixel coordinates (`u`, `v`) measured from the centre of the first
/// cell, or nothing where a contributing cell holds no data.
std::optional<float> Interpolate(const Raster<float>& raster, double u, double v,
                                 ResampleMethod method) {
    if (method == ResampleMethod::Nearest) {
        return raster(Clamp(std::round(u), raster.Width()), Clamp(std::round(v), raster.Height()));
    }
    const double u0 = std::floor(u);
    const double v0 = std::floor(v);
    const double fu = u - u0;
    const double fv = v - v0;
    const int first = method == ResampleMethod::Bilinear ? 0 : -1;
    const int last = method == ResampleMethod::Bilinear ? 1 : 2;
    double sum = 0.0;
    for (int j = first; j <= last; ++j) {
        const double wy =
            method == ResampleMethod::Bilinear ? (j == 0 ? 1.0 - fv : fv) : CubicWeight(fv - j);
        const std::size_t y = Clamp(v0 + j, raster.Height());
        for (int i = first; i <= last; ++i) {
            const double wx =
                method == ResampleMethod::Bilinear ? (i == 0 ? 1.0 - fu : fu) : CubicWeight(fu - i);
            const float value = raster(Clamp(u0 + i, raster.Width()), y);
            if (wx * wy != 0.0 && raster.IsNoData(value)) {
                return std::nullopt;
            }
            sum += wx * wy * static_cast<double>(value);
        }
    }
    return static_cast<float>(sum);
}

void CheckSource(const Raster<float>& raster) {
    if (raster.Empty()) {
        throw std::invalid_argument("Cannot resample an empty raster");
    }
    const GeoTransform& source = raster.Transform();
    if (source.rowRotation != 0.0 || source.colRotation != 0.0) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
}

}  // namespace

float SampleRaster(const Raster<float>& raster, double x, double y, ResampleMethod method) {
    CheckSource(raster);
    const Vec2d pixel = raster.Transform().WorldToPixel(x, y);
    if (!(pixel.x >= 0.0 && pixel.y >= 0.0 && pixel.x < static_cast<double>(raster.Width()) &&
          pixel.y < static_cast<double>(raster.Height()))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const std::optional<float> value = Interpolate(raster, pixel.x - 0.5, pixel.y - 0.5, method);
    if (!value || raster.IsNoData(*value)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return *value;
}

Raster<float> ResampleTo(const Raster<float>& raster, const RasterGrid& grid,
                         ResampleMethod method) {
    CheckSource(raster);
    if (grid.width == 0 || grid.height == 0) {
        throw std::invalid_argument("Cannot resample onto an empty grid");
    }
    Raster<float> result = grid.MakeRaster();
    SampleAtCentres(result, raster.NoData(),
                    [&](double x, double y) { return SampleRaster(raster, x, y, method); });
    return result;
}

std::optional<float> FilledNoData(std::optional<float> noData, bool missing) {
    if (noData || !missing) {
        return noData;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

Raster<float> Resample(const Raster<float>& raster, double pixelSize, ResampleMethod method) {
    if (raster.Empty()) {
        throw std::invalid_argument("Cannot resample an empty raster");
    }
    Raster<float> result = ResampledGrid(RasterGrid::FromRaster(raster), pixelSize).MakeRaster();
    result.SetNoData(raster.NoData());
    const double scaleX = std::fabs(raster.Transform().pixelWidth) / pixelSize;
    const double scaleY = std::fabs(raster.Transform().pixelHeight) / pixelSize;
    const float fill = raster.NoData().value_or(std::nanf(""));
    for (std::size_t row = 0; row < result.Height(); ++row) {
        const double v = (static_cast<double>(row) + 0.5) / scaleY - 0.5;
        for (std::size_t col = 0; col < result.Width(); ++col) {
            const double u = (static_cast<double>(col) + 0.5) / scaleX - 0.5;
            const std::optional<float> value = Interpolate(raster, u, v, method);
            result(col, row) = value.value_or(fill);
        }
    }
    return result;
}

}  // namespace terraingen
