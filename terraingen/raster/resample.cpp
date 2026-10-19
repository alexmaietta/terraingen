#include "terraingen/raster/resample.h"

#include "terraingen/util/arithmetic.h"

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

/// The index in a window of cell `index` of the whole it lies in, clamped to the whole and then to
/// the window.
std::size_t WindowIndex(double index, std::size_t wholeSize, std::size_t first, std::size_t size) {
    const std::size_t whole = ClampIndex(index, wholeSize);
    return whole < first ? 0 : std::min(whole - first, size - 1);
}

/// The larger raster `raster` is placed in interpolated at pixel coordinates (`u`, `v`) measured
/// from the centre of its first cell, read from `raster`, or nothing where a contributing cell
/// holds no data.
std::optional<float> Interpolate(const Raster<float>& raster, const RasterPlacement& placement,
                                 double u, double v, ResampleMethod method) {
    const auto column = [&](double index) {
        return WindowIndex(index, placement.whole.width, placement.col, raster.Width());
    };
    const auto row = [&](double index) {
        return WindowIndex(index, placement.whole.height, placement.row, raster.Height());
    };
    if (method == ResampleMethod::Nearest) {
        return raster(column(std::round(u)), row(std::round(v)));
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
        const std::size_t y = row(v0 + j);
        for (int i = first; i <= last; ++i) {
            const double wx =
                method == ResampleMethod::Bilinear ? (i == 0 ? 1.0 - fu : fu) : CubicWeight(fu - i);
            const float value = raster(column(u0 + i), y);
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
    if (source.IsRotated()) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
}

/// `ResampleWindow` without checks: `cells` of the resampled grid `resampled`.
Raster<float> ResampleCells(const Raster<float>& window, const RasterPlacement& placement,
                            const RasterGrid& resampled, double pixelSize, ResampleMethod method,
                            const PixelWindow& cells) {
    Raster<float> result = Subgrid(resampled, cells).MakeRaster();
    result.SetNoData(window.NoData());
    const double scaleX = std::fabs(placement.whole.transform.pixelWidth) / pixelSize;
    const double scaleY = std::fabs(placement.whole.transform.pixelHeight) / pixelSize;
    const float fill = window.NoData().value_or(std::nanf(""));
    for (std::size_t row = 0; row < result.Height(); ++row) {
        const double v = (static_cast<double>(cells.rowBegin + row) + 0.5) / scaleY - 0.5;
        for (std::size_t col = 0; col < result.Width(); ++col) {
            const double u = (static_cast<double>(cells.colBegin + col) + 0.5) / scaleX - 0.5;
            const std::optional<float> value = Interpolate(window, placement, u, v, method);
            result(col, row) = value.value_or(fill);
        }
    }
    return result;
}

}  // namespace

float SampleRaster(const Raster<float>& raster, double x, double y, ResampleMethod method) {
    return SampleRasterWindow(raster, RasterPlacement::Whole(raster), x, y, method);
}

float SampleRasterWindow(const Raster<float>& window, const RasterPlacement& placement, double x,
                         double y, ResampleMethod method) {
    CheckSource(window);
    const Vec2d pixel = placement.whole.transform.WorldToPixel(x, y);
    if (!(pixel.x >= 0.0 && pixel.y >= 0.0 &&
          pixel.x < static_cast<double>(placement.whole.width) &&
          pixel.y < static_cast<double>(placement.whole.height))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const std::optional<float> value =
        Interpolate(window, placement, pixel.x - 0.5, pixel.y - 0.5, method);
    if (!value || window.IsNoData(*value)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return *value;
}

Raster<float> ResampleWindow(const Raster<float>& window, const RasterPlacement& placement,
                             double pixelSize, ResampleMethod method, const PixelWindow& cells) {
    if (window.Empty()) {
        throw std::invalid_argument("Cannot resample an empty raster");
    }
    const RasterGrid resampled = ResampledGrid(placement.whole, pixelSize);
    if (cells.Empty() || cells.colEnd > resampled.width || cells.rowEnd > resampled.height) {
        throw std::invalid_argument("The cells to resample must be a non-empty window of the "
                                    "resampled grid");
    }
    return ResampleCells(window, placement, resampled, pixelSize, method, cells);
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
    const RasterGrid resampled = ResampledGrid(RasterGrid::FromRaster(raster), pixelSize);
    return ResampleCells(raster, RasterPlacement::Whole(raster), resampled, pixelSize, method,
                         WholeWindow(resampled));
}

}  // namespace terraingen
