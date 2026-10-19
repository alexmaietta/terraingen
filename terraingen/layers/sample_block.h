/// @file
/// @brief Positions a layer evaluates and their heights: a raster's cells or scattered points.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <cstddef>
#include <span>

namespace terraingen {

/// Heights to change in place and where they are: every cell of a raster, sampled at its centre,
/// or a list of scattered points such as wheel contacts. Layers apply to a block in one call, so a
/// single point and a whole grid take the same path and give the same height at the same place.
class SampleBlock {
  public:
    /// The cells of `heights`, which must outlive the block.
    explicit SampleBlock(Raster<float>& heights);

    /// The points `positions`, with their heights in step; both must outlive the block.
    /// @throws std::invalid_argument when the two differ in length.
    SampleBlock(std::span<const Vec2d> positions, std::span<float> heights);

    /// The number of samples.
    std::size_t Size() const {
        return heights_.size();
    }

    /// Whether the samples are a raster's cells.
    bool IsGrid() const {
        return grid_ != nullptr;
    }

    /// The raster of a grid block, or null for points.
    Raster<float>* Grid() const {
        return grid_;
    }

    /// The position of sample `i`: a cell centre, or a point.
    Vec2d Position(std::size_t i) const;

    /// The height of sample `i`.
    float& Height(std::size_t i) const {
        return heights_[i];
    }

    /// Whether sample `i` holds no data: NaN, or a grid's no-data value.
    bool IsNoData(std::size_t i) const;

    /// The smallest box holding every sample: a grid's extent, or the points' bounds, which like
    /// `PointBounds` contain their largest coordinates.
    Bounds2d Bounds() const;

  private:
    Raster<float>* grid_ = nullptr;
    std::span<const Vec2d> positions_;
    std::span<float> heights_;
};

/// Sets each point of `samples` holding data, in order, to `map(height, position)`.
template <typename Map> void MapPoints(SampleBlock& samples, const Map& map) {
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i)) {
            samples.Height(i) = map(samples.Height(i), samples.Position(i));
        }
    }
}

/// Sets each sample of `samples` holding data to `map(height, position)`: a grid's cells over
/// `threads` threads (`MapCells`), or points in order.
template <typename Map> void MapSamples(SampleBlock& samples, unsigned threads, const Map& map) {
    if (Raster<float>* grid = samples.Grid()) {
        MapCells(*grid, threads, map);
        return;
    }
    MapPoints(samples, map);
}

}  // namespace terraingen
