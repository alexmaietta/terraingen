#include "terraingen/sources/flat_source.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

FlatSource::FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation,
                       Vec2d centre)
    : elevation_(elevation) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("A flat source needs at least one cell in each direction");
    }
    RequirePositive(pixelSize, "Pixel size");
    if (!(std::isfinite(elevation) && std::isfinite(centre.x) && std::isfinite(centre.y))) {
        throw std::invalid_argument("Elevation and centre must be finite");
    }
    const double halfX = 0.5 * static_cast<double>(width) * pixelSize;
    const double halfY = 0.5 * static_cast<double>(height) * pixelSize;
    grid_ = {width, height, GeoTransform::NorthUp(centre.x - halfX, centre.y + halfY, pixelSize)};
}

void FlatSource::Fill(Raster<float>& heights) const {
    std::fill(heights.Values().begin(), heights.Values().end(), elevation_);
}

}  // namespace terraingen
