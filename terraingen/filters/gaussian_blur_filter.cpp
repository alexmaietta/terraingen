#include "terraingen/filters/gaussian_blur_filter.h"

#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

GaussianBlurFilter::GaussianBlurFilter(double sigma, Surface surface) : sigma_(sigma) {
    RequireNonNegative(sigma_, "Gaussian blur sigma");
    if (surface.IsSphere()) {
        throw std::invalid_argument("Gaussian blur filters need a plane");
    }
}

void GaussianBlurFilter::Apply(Raster<float>& heights, const FilterContext& /*context*/,
                               const TerrainFeatures& /*features*/) const {
    if (sigma_ == 0.0 || heights.Empty()) {
        return;
    }
    const GeoTransform& transform = heights.Transform();
    heights = GaussianBlur(heights, sigma_ / std::fabs(transform.pixelWidth),
                           sigma_ / std::fabs(transform.pixelHeight));
}

double GaussianBlurFilter::Margin(double /*maxFeatureDiameter*/, double spacing) const {
    return sigma_ == 0.0 ? 0.0 : 3.0 * sigma_ + 2.0 * spacing;
}

}  // namespace terraingen
