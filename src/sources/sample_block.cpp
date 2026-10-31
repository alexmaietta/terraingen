// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/sample_block.h"

#include "terraingen/raster/grid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

SampleBlock::SampleBlock(Raster<float>& heights) : m_grid(&heights), m_heights(heights.Values()) {}

SampleBlock::SampleBlock(Span<const Vec2d> positions, Span<float> heights)
    : m_positions(positions), m_heights(heights) {
    if (positions.size() != heights.size()) {
        throw std::invalid_argument("A sample block needs one height per position");
    }
}

Vec2d SampleBlock::Position(std::size_t i) const {
    if (m_grid != nullptr) {
        return m_grid->CellCenter(i % m_grid->Width(), i / m_grid->Width());
    }
    return m_positions[i];
}

std::vector<Vec2d> SampleBlock::Positions() const {
    if (m_grid == nullptr) {
        return {m_positions.begin(), m_positions.end()};
    }
    std::vector<Vec2d> positions(Size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        positions[i] = Position(i);
    }
    return positions;
}

bool SampleBlock::IsNoData(std::size_t i) const {
    return m_grid != nullptr ? m_grid->IsNoData(m_heights[i]) : std::isnan(m_heights[i]);
}

Bounds2d SampleBlock::Bounds() const {
    if (m_grid != nullptr) {
        return m_grid->Grid().Bounds();
    }
    if (m_positions.empty()) {
        return {};
    }
    Bounds2d box = PointBounds(m_positions[0].x, m_positions[0].y);
    for (const Vec2d& p : m_positions) {
        const Bounds2d point = PointBounds(p.x, p.y);
        box.minX = std::min(box.minX, point.minX);
        box.minY = std::min(box.minY, point.minY);
        box.maxX = std::max(box.maxX, point.maxX);
        box.maxY = std::max(box.maxY, point.maxY);
    }
    return box;
}

double SampleBlock::ResolvedSpacing(double spacing, const Surface& surface) const {
    return m_grid != nullptr ? terraingen::ResolvedSpacing(m_grid->Grid(), spacing, surface)
                             : spacing;
}

}  // namespace terraingen
