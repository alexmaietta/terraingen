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

Raster<double> MaterialSource::MaterialCoverOver(const RasterGrid& grid, double spacing,
                                                 const Ground* ground, unsigned threads) const {
    Raster<double> cover(grid.width, grid.height);
    cover.SetTransform(grid.transform);
    FillCells(cover, threads, [&](const Vec2d& center) {
        return MaterialCoverAt(center.x, center.y, spacing, ground);
    });
    return cover;
}

}  // namespace terraingen
