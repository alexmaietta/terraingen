// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/flat_source.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

FlatSource::FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation,
                       Vec2d center)
    : elevation_(elevation) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("A flat source needs at least one cell in each direction");
    }
    RequirePositive(pixelSize, "Pixel size");
    if (!(std::isfinite(elevation) && std::isfinite(center.x) && std::isfinite(center.y))) {
        throw std::invalid_argument("Elevation and center must be finite");
    }
    const double halfX = 0.5 * static_cast<double>(width) * pixelSize;
    const double halfY = 0.5 * static_cast<double>(height) * pixelSize;
    grid_ = {width, height, GeoTransform::NorthUp(center.x - halfX, center.y + halfY, pixelSize)};
}

void FlatSource::Fill(Raster<float>& heights, double /*spacing*/) const {
    std::fill(heights.Values().begin(), heights.Values().end(), elevation_);
}

}  // namespace terraingen
