#include "terraingen/filter/rim_smoothing.h"

#include "terraingen/raster/filters.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace terraingen {

namespace {}  // namespace

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    if (!(std::isfinite(smoothing.sigmaRadii) && smoothing.sigmaRadii >= 0.0)) {
        throw std::invalid_argument("Rim smoothing sigma must be finite and not negative");
    }
    if (!(smoothing.strength >= 0.0 && smoothing.strength <= 1.0)) {
        throw std::invalid_argument("Rim smoothing strength must be between 0 and 1");
    }
    if (!(std::isfinite(smoothing.minDiameterPixels) && smoothing.minDiameterPixels >= 0.0)) {
        throw std::invalid_argument("minDiameterPixels must be finite and not negative");
    }
    if (!(smoothing.innerRadii >= 0.0 && smoothing.innerRadii < 1.0 && smoothing.outerRadii > 1.0 &&
          std::isfinite(smoothing.outerRadii))) {
        throw std::invalid_argument("Rim smoothing band must satisfy 0 <= inner < 1 < outer");
    }
}

namespace {

/// Smoothing weight at `radii` crater radii: 1 at the rim, falling to 0 at both band edges.
double BandWeight(double radii, const RimSmoothing& smoothing) {
    if (radii <= 1.0) {
        return CraterBlendWeight(2.0 - radii, {1.0, 2.0 - smoothing.innerRadii, smoothing.shape});
    }
    return CraterBlendWeight(radii, {1.0, smoothing.outerRadii, smoothing.shape});
}

}  // namespace

void SmoothCraterRims(Raster<float>& raster, const CraterList& craters,
                      const RimSmoothing& smoothing) {
    ValidateRimSmoothing(smoothing);
    if (raster.Empty() || craters.Empty() || smoothing.sigmaRadii == 0.0 ||
        smoothing.strength == 0.0) {
        return;
    }
    const GeoTransform& transform = raster.Transform();
    const double pixelArea = std::fabs(transform.pixelWidth * transform.pixelHeight -
                                       transform.rowRotation * transform.colRotation);
    if (!(pixelArea > 0.0)) {
        throw std::domain_error("The raster's transform is not invertible");
    }
    const double pixelSize = std::sqrt(pixelArea);
    const std::optional<float> noData = raster.NoData();

    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        if (crater.diameter < smoothing.minDiameterPixels * pixelSize) {
            continue;
        }
        const double radius = 0.5 * crater.diameter;
        const double sigma = smoothing.sigmaRadii * radius;
        const double reach = smoothing.outerRadii * radius + 3.0 * sigma;
        constexpr double kInf = std::numeric_limits<double>::infinity();
        double colMin = kInf;
        double colMax = -kInf;
        double rowMin = kInf;
        double rowMax = -kInf;
        for (const double dx : {-reach, reach}) {
            for (const double dy : {-reach, reach}) {
                const Vec2d pixel = transform.WorldToPixel(crater.x + dx, crater.y + dy);
                colMin = std::fmin(colMin, pixel.x);
                colMax = std::fmax(colMax, pixel.x);
                rowMin = std::fmin(rowMin, pixel.y);
                rowMax = std::fmax(rowMax, pixel.y);
            }
        }
        const auto clampTo = [](double value, std::size_t limit) {
            return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
        };
        const std::size_t col0 = clampTo(std::floor(colMin), raster.Width());
        const std::size_t col1 = clampTo(std::ceil(colMax), raster.Width());
        const std::size_t row0 = clampTo(std::floor(rowMin), raster.Height());
        const std::size_t row1 = clampTo(std::ceil(rowMax), raster.Height());
        if (col0 >= col1 || row0 >= row1) {
            continue;
        }

        Raster<float> window(col1 - col0, row1 - row0);
        window.SetNoData(noData);
        for (std::size_t row = row0; row < row1; ++row) {
            for (std::size_t col = col0; col < col1; ++col) {
                window(col - col0, row - row0) = raster(col, row);
            }
        }
        const Raster<float> smoothed = GaussianBlur(window, sigma / pixelSize);
        for (std::size_t row = row0; row < row1; ++row) {
            for (std::size_t col = col0; col < col1; ++col) {
                float& cell = raster(col, row);
                if (raster.IsNoData(cell)) {
                    continue;
                }
                const Vec2d centre = raster.CellCenter(col, row);
                const double radii = std::hypot(centre.x - crater.x, centre.y - crater.y) / radius;
                if (radii <= smoothing.innerRadii || radii >= smoothing.outerRadii) {
                    continue;
                }
                const double weight = smoothing.strength * BandWeight(radii, smoothing);
                const double height = cell;
                const double target = smoothed(col - col0, row - row0);
                cell = static_cast<float>(height + weight * (target - height));
            }
        }
    }
}

}  // namespace terraingen
