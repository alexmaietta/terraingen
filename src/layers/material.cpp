// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/material.h"

#include "terraingen/raster/cell_loops.h"

#include <algorithm>
#include <cstddef>

namespace terraingen {

MaterialShares SoilShares() {
    MaterialShares shares{};
    shares[static_cast<std::size_t>(MaterialClass::Soil)] = 1.0;
    return shares;
}

void CoverWith(MaterialShares& shares, MaterialClass material, double cover) {
    const double weight = std::clamp(cover, 0.0, 1.0);
    for (double& share : shares) {
        share *= 1.0 - weight;
    }
    shares[static_cast<std::size_t>(material)] += weight;
}

Raster<double> MaterialSource::MaterialCoverOver(const RasterGrid& grid,
                                                 const LayerContext& context) const {
    Raster<double> cover(grid);
    MapCells(cover, context.threads, [&](double, const Vec2d& center) {
        return MaterialCoverAt(center.x, center.y, context);
    });
    return cover;
}

}  // namespace terraingen
