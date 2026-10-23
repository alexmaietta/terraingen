#include "terraingen/raster/resample.h"

#include "terraingen/util/arithmetic.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace terraingen {

namespace {

/// Keys cubic convolution kernel with a = -0.5, the Catmull-Rom spline.
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

/// sin(pi x) / (pi x), 1 at 0.
double Sinc(double x) {
    if (x == 0.0) {
        return 1.0;
    }
    const double angle = kPi * x;
    return std::sin(angle) / angle;
}

/// The Lanczos kernel with three lobes, exactly 0 at the whole offsets but 0.
double Lanczos3Weight(double x) {
    x = std::fabs(x);
    if (x >= 3.0 || (x > 0.0 && x == std::floor(x))) {
        return 0.0;
    }
    return Sinc(x) * Sinc(x / 3.0);
}

/// The uniform cubic B-spline.
double BSplineWeight(double x) {
    x = std::fabs(x);
    if (x < 1.0) {
        return 2.0 / 3.0 - x * x + 0.5 * x * x * x;
    }
    if (x < 2.0) {
        const double rest = 2.0 - x;
        return rest * rest * rest / 6.0;
    }
    return 0.0;
}

/// `raster` interpolated at pixel coordinates (`u`, `v`) measured from the center of its first
/// cell, with cells beyond the edge read from the nearest edge cell; nothing where a contributing
/// cell holds no data.
std::optional<float> Interpolate(const Raster<float>& raster, double u, double v,
                                 ResampleMethod method) {
    const auto value = [&](double col, double row) {
        return raster(ClampIndex(col, raster.Width()), ClampIndex(row, raster.Height()));
    };
    return InterpolateLattice(u, v, method, value,
                              [&](float cell) { return raster.IsNoData(cell); });
}

/// Checks that a raster can be resampled: not empty and not rotated.
void CheckSource(const Raster<float>& raster) {
    if (raster.Empty()) {
        throw std::invalid_argument("Cannot resample an empty raster");
    }
    if (raster.Transform().IsRotated()) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
}

}  // namespace

int KernelRadius(ResampleMethod method) {
    switch (method) {
    case ResampleMethod::Nearest:
    case ResampleMethod::Bilinear:
        return 1;
    case ResampleMethod::Lanczos3:
        return 3;
    case ResampleMethod::Cubic:
    case ResampleMethod::CatmullRom:
    case ResampleMethod::CubicBSpline:
        break;
    }
    return 2;
}

double detail::KernelWeight(ResampleMethod method, double x) {
    switch (method) {
    case ResampleMethod::Lanczos3:
        return Lanczos3Weight(x);
    case ResampleMethod::CubicBSpline:
        return BSplineWeight(x);
    default:
        return CubicWeight(x);
    }
}

std::array<double, 6> KernelWeights(ResampleMethod method, double fraction) {
    std::array<double, 6> weights{};
    if (method == ResampleMethod::Nearest) {
        return weights;
    }
    if (method == ResampleMethod::Bilinear) {
        weights[0] = 1.0 - fraction;
        weights[1] = fraction;
        return weights;
    }
    const int radius = KernelRadius(method);
    double total = 0.0;
    for (int i = 1 - radius; i <= radius; ++i) {
        const double weight = detail::KernelWeight(method, fraction - i);
        weights[static_cast<std::size_t>(i + radius - 1)] = weight;
        total += weight;
    }
    if (method == ResampleMethod::Lanczos3) {
        for (double& weight : weights) {
            weight /= total;
        }
    }
    return weights;
}

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

std::optional<float> FilledNoData(std::optional<float> noData, bool missing) {
    if (noData || !missing) {
        return noData;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

}  // namespace terraingen
