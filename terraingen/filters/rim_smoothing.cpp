#include "terraingen/filters/rim_smoothing.h"

#include "terraingen/raster/filters.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

namespace terraingen {

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    RequireNonNegative(smoothing.sigmaRadii, "Rim smoothing sigma");
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
                      const RimSmoothing& smoothing, double spacing, unsigned threads,
                      const Surface& surface) {
    ValidateRimSmoothing(smoothing);
    RequireNonNegative(spacing, "Sample spacing");
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
    const double resolved =
        std::max(spacing, surface.IsSphere() ? RasterGrid::Of(raster).Spacing(surface) : pixelSize);
    const std::optional<float> noData = raster.NoData();

    /// One window of a crater's band of cells and the terrain there blurred before any smoothing.
    struct Piece {
        std::size_t col0 = 0;
        std::size_t col1 = 0;
        std::size_t row0 = 0;
        std::size_t row1 = 0;
        Raster<float> blurred;
    };
    struct Smoothed {
        double resolution = 0.0;
        std::vector<Piece> pieces;
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
        double sigmaColumns = sigma / pixelSize;
        double sigmaRows = sigma / pixelSize;
        Bounds2d box{crater.x - reach, crater.y - reach, crater.x + reach, crater.y + reach};
        std::vector<double> shifts{0.0};
        if (surface.IsSphere()) {
            box = surface.Expand({crater.x, crater.y, crater.x, crater.y}, reach);
            shifts = {-360.0, 0.0, 360.0};
            const double unit = surface.UnitLength();
            const double cosLat = std::fmax(std::cos(crater.y * std::numbers::pi / 180.0), 1e-6);
            sigmaColumns = sigma / (std::fabs(transform.pixelWidth) * unit * cosLat);
            sigmaRows = sigma / (std::fabs(transform.pixelHeight) * unit);
        }
        const auto clampTo = [](double value, std::size_t limit) {
            return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
        };
        for (const double shift : shifts) {
            constexpr double kInf = std::numeric_limits<double>::infinity();
            double colMin = kInf;
            double colMax = -kInf;
            double rowMin = kInf;
            double rowMax = -kInf;
            for (const double wx : {box.minX + shift, box.maxX + shift}) {
                for (const double wy : {box.minY, box.maxY}) {
                    const Vec2d pixel = transform.WorldToPixel(wx, wy);
                    colMin = std::fmin(colMin, pixel.x);
                    colMax = std::fmax(colMax, pixel.x);
                    rowMin = std::fmin(rowMin, pixel.y);
                    rowMax = std::fmax(rowMax, pixel.y);
                }
            }
            Piece piece;
            piece.col0 = clampTo(std::floor(colMin), raster.Width());
            piece.col1 = clampTo(std::ceil(colMax), raster.Width());
            piece.row0 = clampTo(std::floor(rowMin), raster.Height());
            piece.row1 = clampTo(std::ceil(rowMax), raster.Height());
            if (piece.col0 >= piece.col1 || piece.row0 >= piece.row1) {
                continue;
            }
            Raster<float> window(piece.col1 - piece.col0, piece.row1 - piece.row0);
            window.SetNoData(noData);
            for (std::size_t row = piece.row0; row < piece.row1; ++row) {
                for (std::size_t col = piece.col0; col < piece.col1; ++col) {
                    window(col - piece.col0, row - piece.row0) = raster(col, row);
                }
            }
            piece.blurred = GaussianBlur(window, sigmaColumns, sigmaRows);
            smoothed[n].pieces.push_back(std::move(piece));
        }
        smoothed[n].resolution = smoothed[n].pieces.empty() ? 0.0 : resolution;
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
            for (const Piece& piece : entry.pieces) {
                const std::size_t rowBegin = std::max(bandBegin, piece.row0);
                const std::size_t rowEnd = std::min(bandEnd, piece.row1);
                for (std::size_t row = rowBegin; row < rowEnd; ++row) {
                    for (std::size_t col = piece.col0; col < piece.col1; ++col) {
                        float& cell = raster(col, row);
                        if (raster.IsNoData(cell)) {
                            continue;
                        }
                        const Vec2d centre = raster.CellCenter(col, row);
                        const double distance =
                            surface.IsSphere()
                                ? surface.Distance({crater.x, crater.y}, centre)
                                : std::hypot(centre.x - crater.x, centre.y - crater.y);
                        const double radii = distance / radius;
                        if (radii <= smoothing.innerRadii || radii >= smoothing.outerRadii) {
                            continue;
                        }
                        const double weight =
                            entry.resolution * smoothing.strength * BandWeight(radii, smoothing);
                        const double height = cell;
                        const double target = piece.blurred(col - piece.col0, row - piece.row0);
                        cell = static_cast<float>(height + weight * (target - height));
                    }
                }
            }
        }
    });
}

RimSmoothingFilter::RimSmoothingFilter(const RimSmoothing& smoothing, const Surface& surface)
    : smoothing_(smoothing), surface_(surface) {
    ValidateRimSmoothing(smoothing_);
}

void RimSmoothingFilter::Apply(Raster<float>& heights, double spacing,
                               const TerrainFeatures& features) const {
    SmoothCraterRims(heights, features.craters, smoothing_, spacing, 1, surface_);
}

void RimSmoothingFilter::ApplyParallel(Raster<float>& heights, double spacing,
                                       const TerrainFeatures& features, unsigned threads) const {
    SmoothCraterRims(heights, features.craters, smoothing_, spacing, threads, surface_);
}

double RimSmoothingFilter::Margin(double maxFeatureDiameter, double spacing) const {
    if (smoothing_.sigmaRadii == 0.0 || smoothing_.strength == 0.0) {
        return 0.0;
    }
    const double radius = 0.5 * maxFeatureDiameter;
    return (smoothing_.outerRadii + 3.0 * smoothing_.sigmaRadii) * radius + 2.0 * spacing;
}

}  // namespace terraingen
