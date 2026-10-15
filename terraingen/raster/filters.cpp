#include "terraingen/raster/filters.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace terraingen {

namespace {

std::vector<double> GaussianKernel(double sigma) {
    if (sigma == 0.0) {
        return {1.0};
    }
    const auto radius = static_cast<std::ptrdiff_t>(std::ceil(3.0 * sigma));
    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    for (std::ptrdiff_t i = -radius; i <= radius; ++i) {
        const auto x = static_cast<double>(i);
        kernel[static_cast<std::size_t>(i + radius)] = std::exp(-0.5 * x * x / (sigma * sigma));
    }
    return kernel;
}

/// Convolves `values` along one axis of a `width` x `height` grid with `kernel`, truncated at the
/// grid's edges.
std::vector<double> Convolve(const std::vector<double>& values, std::size_t width,
                             std::size_t height, const std::vector<double>& kernel,
                             bool alongRows) {
    const auto radius = static_cast<std::ptrdiff_t>(kernel.size() / 2);
    const auto length = static_cast<std::ptrdiff_t>(alongRows ? width : height);
    std::vector<double> result(values.size(), 0.0);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const auto position = static_cast<std::ptrdiff_t>(alongRows ? col : row);
            double sum = 0.0;
            for (std::ptrdiff_t k = -radius; k <= radius; ++k) {
                const std::ptrdiff_t at = position + k;
                if (at < 0 || at >= length) {
                    continue;
                }
                const std::size_t index = alongRows ? row * width + static_cast<std::size_t>(at)
                                                    : static_cast<std::size_t>(at) * width + col;
                sum += kernel[static_cast<std::size_t>(k + radius)] * values[index];
            }
            result[row * width + col] = sum;
        }
    }
    return result;
}

}  // namespace

Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaPixels) {
    return GaussianBlur(raster, sigmaPixels, sigmaPixels);
}

Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaColumns, double sigmaRows) {
    if (!(IsNonNegative(sigmaColumns) && IsNonNegative(sigmaRows))) {
        throw std::invalid_argument("Gaussian sigma must be finite and not negative");
    }
    Raster<float> result = raster;
    if ((sigmaColumns == 0.0 && sigmaRows == 0.0) || raster.Empty()) {
        return result;
    }
    const std::size_t width = raster.Width();
    const std::size_t height = raster.Height();
    std::vector<double> weighted(raster.Size());
    std::vector<double> mask(raster.Size());
    for (std::size_t i = 0; i < raster.Size(); ++i) {
        const float value = raster.Values()[i];
        const bool valid = !raster.IsNoData(value);
        mask[i] = valid ? 1.0 : 0.0;
        weighted[i] = valid ? static_cast<double>(value) : 0.0;
    }
    const std::vector<double> across = GaussianKernel(sigmaColumns);
    const std::vector<double> down = GaussianKernel(sigmaRows);
    const std::vector<double> numerator =
        Convolve(Convolve(weighted, width, height, across, true), width, height, down, false);
    const std::vector<double> denominator =
        Convolve(Convolve(mask, width, height, across, true), width, height, down, false);
    for (std::size_t i = 0; i < raster.Size(); ++i) {
        if (mask[i] > 0.0) {
            result.Values()[i] = static_cast<float>(numerator[i] / denominator[i]);
        }
    }
    return result;
}

}  // namespace terraingen
