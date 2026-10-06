#include "terraingen/filters/rim_smoothing.h"

#include "terraingen/raster/filters.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace terraingen {

namespace {}  // namespace

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    if (!(std::isfinite(smoothing.sigmaRadii) && smoothing.sigmaRadii >= 0.0)) {
        throw std::invalid_argument("Rim smoothing sigma must be finite and not negative");
    }
    if (!(smoothing.strength >= 0.0 && smoothing.strength <= 1.0)) {
        throw std::invalid_argument("Rim smoothing strength must be between 0 and 1");
    }
    ValidateResolutionFade(smoothing.fade);
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
                      const RimSmoothing& smoothing, double spacing, unsigned threads) {
    ValidateRimSmoothing(smoothing);
    if (!(std::isfinite(spacing) && spacing >= 0.0)) {
        throw std::invalid_argument("Sample spacing must be finite and not negative");
    }
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
    const double resolved = std::max(spacing, pixelSize);
    const std::optional<float> noData = raster.NoData();

    /// A crater's band of cells and the terrain there blurred before any smoothing.
    struct Smoothed {
        double resolution = 0.0;
        std::size_t col0 = 0;
        std::size_t col1 = 0;
        std::size_t row0 = 0;
        std::size_t row1 = 0;
        Raster<float> blurred;
    };
    std::vector<Smoothed> smoothed(craters.Size());
    ParallelFor(craters.Size(), threads, [&](std::size_t n) {
        const Crater crater = craters.At(n);
        const double resolution = ResolutionWeight(crater.diameter, resolved, smoothing.fade);
        if (resolution == 0.0) {
            return;
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
        Smoothed& entry = smoothed[n];
        entry.col0 = clampTo(std::floor(colMin), raster.Width());
        entry.col1 = clampTo(std::ceil(colMax), raster.Width());
        entry.row0 = clampTo(std::floor(rowMin), raster.Height());
        entry.row1 = clampTo(std::ceil(rowMax), raster.Height());
        if (entry.col0 >= entry.col1 || entry.row0 >= entry.row1) {
            return;
        }
        Raster<float> window(entry.col1 - entry.col0, entry.row1 - entry.row0);
        window.SetNoData(noData);
        for (std::size_t row = entry.row0; row < entry.row1; ++row) {
            for (std::size_t col = entry.col0; col < entry.col1; ++col) {
                window(col - entry.col0, row - entry.row0) = raster(col, row);
            }
        }
        entry.blurred = GaussianBlur(window, sigma / pixelSize);
        entry.resolution = resolution;
    });

    constexpr std::size_t kBandRows = 32;
    const std::size_t bands = (raster.Height() + kBandRows - 1) / kBandRows;
    ParallelFor(bands, threads, [&](std::size_t band) {
        const std::size_t bandBegin = band * kBandRows;
        const std::size_t bandEnd = std::min(raster.Height(), bandBegin + kBandRows);
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Smoothed& entry = smoothed[n];
            if (entry.resolution == 0.0) {
                continue;
            }
            const Crater crater = craters.At(n);
            const double radius = 0.5 * crater.diameter;
            const std::size_t rowBegin = std::max(bandBegin, entry.row0);
            const std::size_t rowEnd = std::min(bandEnd, entry.row1);
            for (std::size_t row = rowBegin; row < rowEnd; ++row) {
                for (std::size_t col = entry.col0; col < entry.col1; ++col) {
                    float& cell = raster(col, row);
                    if (raster.IsNoData(cell)) {
                        continue;
                    }
                    const Vec2d centre = raster.CellCenter(col, row);
                    const double radii =
                        std::hypot(centre.x - crater.x, centre.y - crater.y) / radius;
                    if (radii <= smoothing.innerRadii || radii >= smoothing.outerRadii) {
                        continue;
                    }
                    const double weight =
                        entry.resolution * smoothing.strength * BandWeight(radii, smoothing);
                    const double height = cell;
                    const double target = entry.blurred(col - entry.col0, row - entry.row0);
                    cell = static_cast<float>(height + weight * (target - height));
                }
            }
        }
    });
}

RimSmoothingFilter::RimSmoothingFilter(const RimSmoothing& smoothing) : smoothing_(smoothing) {
    ValidateRimSmoothing(smoothing_);
}

void RimSmoothingFilter::Apply(Raster<float>& heights, double spacing,
                               const TerrainFeatures& features) const {
    SmoothCraterRims(heights, features.craters, smoothing_, spacing);
}

void RimSmoothingFilter::ApplyParallel(Raster<float>& heights, double spacing,
                                       const TerrainFeatures& features, unsigned threads) const {
    SmoothCraterRims(heights, features.craters, smoothing_, spacing, threads);
}

double RimSmoothingFilter::Margin(double maxFeatureDiameter, double spacing) const {
    if (smoothing_.sigmaRadii == 0.0 || smoothing_.strength == 0.0) {
        return 0.0;
    }
    const double radius = 0.5 * maxFeatureDiameter;
    return (smoothing_.outerRadii + 3.0 * smoothing_.sigmaRadii) * radius + 2.0 * spacing;
}

}  // namespace terraingen
