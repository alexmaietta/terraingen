#include "terraingen/albedo/brightness_statistics.h"

#include <cmath>
#include <limits>

namespace terraingen {

namespace {

/// The logarithm of the mean of the bands at a cell, or NaN where it has no data.
double LogBrightness(const std::vector<Raster<float>>& bands, std::size_t col, std::size_t row) {
    double sum = 0.0;
    for (const Raster<float>& band : bands) {
        const float value = band(col, row);
        if (band.IsNoData(value) || !std::isfinite(value) || value <= 0.0f) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        sum += static_cast<double>(value);
    }
    return std::log(sum / static_cast<double>(bands.size()));
}

}  // namespace

std::vector<double> LogBrightnessGrid(const std::vector<Raster<float>>& bands) {
    const std::size_t width = bands.front().Width();
    const std::size_t height = bands.front().Height();
    std::vector<double> logs(width * height);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            logs[row * width + col] = LogBrightness(bands, col, row);
        }
    }
    return logs;
}

std::optional<double> StructureFunction(const std::vector<double>& values, std::size_t width,
                                        std::size_t height, std::size_t lag) {
    double sum = 0.0;
    std::size_t pairs = 0;
    const auto add = [&](double a, double b) {
        if (std::isfinite(a) && std::isfinite(b)) {
            sum += (a - b) * (a - b);
            ++pairs;
        }
    };
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const double here = values[row * width + col];
            if (col + lag < width) {
                add(here, values[row * width + col + lag]);
            }
            if (row + lag < height) {
                add(here, values[(row + lag) * width + col]);
            }
        }
    }
    if (!(pairs > 0 && sum > 0.0)) {
        return std::nullopt;
    }
    return sum / static_cast<double>(pairs);
}

double FitSlope(const std::vector<double>& x, const std::vector<double>& y) {
    const auto n = static_cast<double>(x.size());
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        mx += x[i] / n;
        my += y[i] / n;
    }
    double sxy = 0.0;
    double sxx = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sxy += (x[i] - mx) * (y[i] - my);
        sxx += (x[i] - mx) * (x[i] - mx);
    }
    return sxy / sxx;
}

}  // namespace terraingen
