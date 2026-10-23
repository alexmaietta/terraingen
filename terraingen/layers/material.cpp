#include "terraingen/layers/material.h"

#include "terraingen/util/parallel.h"

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
    ParallelFor(grid.height, threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d center = cover.CellCenter(col, row);
            cover(col, row) = MaterialCoverAt(center.x, center.y, spacing, ground);
        }
    });
    return cover;
}

}  // namespace terraingen
