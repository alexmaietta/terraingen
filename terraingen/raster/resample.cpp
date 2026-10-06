#include "terraingen/raster/resample.h"

#include <algorithm>
#include <array>
#include <cmath>
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

}  // namespace

Raster<float> Resample(const Raster<float>& raster, double pixelSize, ResampleMethod method) {
    if (raster.Empty()) {
        throw std::invalid_argument("Cannot resample an empty raster");
    }
    if (!(std::isfinite(pixelSize) && pixelSize > 0.0)) {
        throw std::invalid_argument("Pixel size must be positive and finite");
    }
    const GeoTransform& source = raster.Transform();
    if (source.rowRotation != 0.0 || source.colRotation != 0.0) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
    const double scaleX = std::fabs(source.pixelWidth) / pixelSize;
    const double scaleY = std::fabs(source.pixelHeight) / pixelSize;
    const double width = std::max(1.0, std::round(static_cast<double>(raster.Width()) * scaleX));
    const double height = std::max(1.0, std::round(static_cast<double>(raster.Height()) * scaleY));
    if (width > 2147483647.0 || height > 2147483647.0) {
        throw std::invalid_argument("Resampled raster would be too large");
    }
    Raster<float> result(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
    GeoTransform transform = source;
    transform.pixelWidth = std::copysign(pixelSize, source.pixelWidth);
    transform.pixelHeight = std::copysign(pixelSize, source.pixelHeight);
    result.SetTransform(transform);
    result.SetCrs(raster.Crs());
    result.SetNoData(raster.NoData());
    const std::optional<float> noData = raster.NoData();
    const float fill = noData.value_or(std::nanf(""));

    for (std::size_t row = 0; row < result.Height(); ++row) {
        const double v = (static_cast<double>(row) + 0.5) / scaleY - 0.5;
        for (std::size_t col = 0; col < result.Width(); ++col) {
            const double u = (static_cast<double>(col) + 0.5) / scaleX - 0.5;
            float& out = result(col, row);
            if (method == ResampleMethod::Nearest) {
                out = raster(Clamp(std::round(u), raster.Width()),
                             Clamp(std::round(v), raster.Height()));
                continue;
            }
            const double u0 = std::floor(u);
            const double v0 = std::floor(v);
            const double fu = u - u0;
            const double fv = v - v0;
            const int first = method == ResampleMethod::Bilinear ? 0 : -1;
            const int last = method == ResampleMethod::Bilinear ? 1 : 2;
            double sum = 0.0;
            bool missing = false;
            for (int j = first; j <= last && !missing; ++j) {
                const double wy = method == ResampleMethod::Bilinear ? (j == 0 ? 1.0 - fv : fv)
                                                                     : CubicWeight(fv - j);
                const std::size_t y = Clamp(v0 + j, raster.Height());
                for (int i = first; i <= last; ++i) {
                    const double wx = method == ResampleMethod::Bilinear ? (i == 0 ? 1.0 - fu : fu)
                                                                         : CubicWeight(fu - i);
                    const float value = raster(Clamp(u0 + i, raster.Width()), y);
                    if (wx * wy != 0.0 && raster.IsNoData(value)) {
                        missing = true;
                        break;
                    }
                    sum += wx * wy * static_cast<double>(value);
                }
            }
            out = missing ? fill : static_cast<float>(sum);
        }
    }
    return result;
}

}  // namespace terraingen
