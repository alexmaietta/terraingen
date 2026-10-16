/// @file
/// @brief Rounding crater rims by blending them toward a blurred copy of the terrain.
#include "terraingen/filters/rim_smoothing.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// One crater's band of cells, in windows, and the terrain there blurred before any smoothing.
struct BlurredRim {
    Crater crater;
    double resolution = 0.0;  ///< 0 when the crater is unresolved or off the raster.
    std::vector<PixelWindow> windows;
    std::vector<Raster<float>> blurred;  ///< One per window.
};

/// Smoothing weight at `radii` crater radii: 1 at the rim, falling to 0 at both band edges.
double BandWeight(double radii, const RimSmoothing& smoothing) {
    if (radii <= 1.0) {
        return Falloff(2.0 - radii, 1.0, 2.0 - smoothing.innerRadii, smoothing.shape);
    }
    return Falloff(radii, 1.0, smoothing.outerRadii, smoothing.shape);
}

/// The cells of `window` of `raster` blurred by a Gaussian of `sigmaColumns` and `sigmaRows`.
Raster<float> BlurWindow(const Raster<float>& raster, const PixelWindow& window,
                         double sigmaColumns, double sigmaRows) {
    Raster<float> cells(window.colEnd - window.colBegin, window.rowEnd - window.rowBegin);
    cells.SetNoData(raster.NoData());
    for (std::size_t row = window.rowBegin; row < window.rowEnd; ++row) {
        for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
            cells(col - window.colBegin, row - window.rowBegin) = raster(col, row);
        }
    }
    return GaussianBlur(cells, sigmaColumns, sigmaRows);
}

/// The terrain around `crater`'s rim band blurred by its sigma, `sigmaRadii` of its radius: in
/// cells of side `pixelSize` on a plane, and on a sphere narrower in columns by the cosine of its
/// latitude, so the blur is round in metres.
BlurredRim BlurAround(const Raster<float>& raster, const Crater& crater,
                      const RimSmoothing& smoothing, double pixelSize, const Surface& surface) {
    const double radius = 0.5 * crater.diameter;
    const double sigma = smoothing.sigmaRadii * radius;
    const double reach = smoothing.outerRadii * radius + 3.0 * sigma;
    double sigmaColumns = sigma / pixelSize;
    double sigmaRows = sigma / pixelSize;
    if (surface.IsSphere()) {
        const GeoTransform& transform = raster.Transform();
        const double unit = surface.UnitLength();
        const double cosLat = std::fmax(std::cos(crater.y * std::numbers::pi / 180.0), 1e-6);
        sigmaColumns = sigma / (std::fabs(transform.pixelWidth) * unit * cosLat);
        sigmaRows = sigma / (std::fabs(transform.pixelHeight) * unit);
    }
    BlurredRim rim;
    rim.crater = crater;
    rim.windows = WindowsAround(
        raster, surface.Expand({crater.x, crater.y, crater.x, crater.y}, reach), surface);
    for (const PixelWindow& window : rim.windows) {
        rim.blurred.push_back(BlurWindow(raster, window, sigmaColumns, sigmaRows));
    }
    return rim;
}

/// `cell` moved toward the blurred terrain of `rim` by the smoothing weight at its distance.
float BlendTowardBlur(float cell, const FeatureCell& at, const BlurredRim& rim,
                      const RimSmoothing& smoothing, const Surface& surface) {
    const Crater& crater = rim.crater;
    const double radii =
        surface.Distance({crater.x, crater.y}, at.centre) / (0.5 * crater.diameter);
    if (radii <= smoothing.innerRadii || radii >= smoothing.outerRadii) {
        return cell;
    }
    const double weight = rim.resolution * smoothing.strength * BandWeight(radii, smoothing);
    const double height = cell;
    const PixelWindow& window = rim.windows[at.window];
    const double target =
        rim.blurred[at.window](at.col - window.colBegin, at.row - window.rowBegin);
    return static_cast<float>(height + weight * (target - height));
}

}  // namespace

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

void SmoothCraterRims(Raster<float>& raster, const CraterList& craters,
                      const RimSmoothing& smoothing, double spacing, unsigned threads,
                      const Surface& surface) {
    ValidateRimSmoothing(smoothing);
    RequireNonNegative(spacing, "Sample spacing");
    if (raster.Empty() || craters.Empty() || smoothing.sigmaRadii == 0.0 ||
        smoothing.strength == 0.0) {
        return;
    }
    const double resolved = ResolvedSpacing(raster, spacing, surface);
    const double pixelSize = RasterGrid::Of(raster).PixelSize();
    std::vector<BlurredRim> rims(craters.Size());
    ParallelFor(craters.Size(), threads, [&](std::size_t n) {
        const Crater crater = craters.At(n);
        const double resolution = ResolutionWeight(crater.diameter, resolved, smoothing.fade);
        if (resolution == 0.0) {
            return;
        }
        rims[n] = BlurAround(raster, crater, smoothing, pixelSize, surface);
        rims[n].resolution = rims[n].windows.empty() ? 0.0 : resolution;
    });
    ForEachFeatureCell(
        raster, threads, rims.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return rims[n].windows; },
        [&](float& cell, const FeatureCell& at) {
            cell = BlendTowardBlur(cell, at, rims[at.feature], smoothing, surface);
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
