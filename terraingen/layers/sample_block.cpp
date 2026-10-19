#include "terraingen/layers/sample_block.h"

#include "terraingen/raster/grid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

SampleBlock::SampleBlock(Raster<float>& heights) : grid_(&heights), heights_(heights.Values()) {}

SampleBlock::SampleBlock(Span<const Vec2d> positions, Span<float> heights)
    : positions_(positions), heights_(heights) {
    if (positions.size() != heights.size()) {
        throw std::invalid_argument("A sample block needs one height per position");
    }
}

Vec2d SampleBlock::Position(std::size_t i) const {
    if (grid_ != nullptr) {
        return grid_->CellCenter(i % grid_->Width(), i / grid_->Width());
    }
    return positions_[i];
}

bool SampleBlock::IsNoData(std::size_t i) const {
    return grid_ != nullptr ? grid_->IsNoData(heights_[i]) : std::isnan(heights_[i]);
}

Bounds2d SampleBlock::Bounds() const {
    if (grid_ != nullptr) {
        return RasterGrid::FromRaster(*grid_).Bounds();
    }
    if (positions_.empty()) {
        return {};
    }
    Bounds2d box = PointBounds(positions_[0].x, positions_[0].y);
    for (const Vec2d& p : positions_) {
        const Bounds2d point = PointBounds(p.x, p.y);
        box.minX = std::min(box.minX, point.minX);
        box.minY = std::min(box.minY, point.minY);
        box.maxX = std::max(box.maxX, point.maxX);
        box.maxY = std::max(box.maxY, point.maxY);
    }
    return box;
}

double SampleBlock::ResolvedSpacing(double spacing, const Surface& surface) const {
    return grid_ != nullptr
               ? terraingen::ResolvedSpacing(RasterGrid::FromRaster(*grid_), spacing, surface)
               : spacing;
}

}  // namespace terraingen
