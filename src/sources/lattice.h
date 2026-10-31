// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Windows of post lattices, tiled and gathered fills, point queries, exact subgrids and
/// missing-value marking shared by sources, upsamplers and grounds.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/sources/upsampling.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace terraingen {

namespace detail {

/// How close to a whole number of posts or nodes a lattice position counts as one.
inline constexpr double kOnLattice = 1e-6;

/// `value` as an integer when it is within `kOnLattice` of one.
inline std::optional<std::int64_t> NearestWhole(double value) {
    const double rounded = std::round(value);
    if (!(std::fabs(value - rounded) <= kOnLattice)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(rounded);
}

/// An inclusive box of lattice indices, columns [`col0`, `col1`] and rows [`row0`, `row1`];
/// empty until it includes a span.
struct IndexBox {
    std::int64_t col0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t row0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t col1 = std::numeric_limits<std::int64_t>::min();
    std::int64_t row1 = std::numeric_limits<std::int64_t>::min();

    /// Grows the box to hold columns [`first`, `last`] and rows [`top`, `bottom`].
    void Include(std::int64_t first, std::int64_t last, std::int64_t top, std::int64_t bottom) {
        col0 = std::min(col0, first);
        col1 = std::max(col1, last);
        row0 = std::min(row0, top);
        row1 = std::max(row1, bottom);
    }

    /// Columns in a non-empty box.
    std::size_t Columns() const {
        return static_cast<std::size_t>(col1 - col0 + 1);
    }

    /// Rows in a non-empty box.
    std::size_t Rows() const {
        return static_cast<std::size_t>(row1 - row0 + 1);
    }

    /// The number of indices the box holds, as a double so that it cannot overflow.
    double Area() const {
        return static_cast<double>(col1 - col0 + 1) * static_cast<double>(row1 - row0 + 1);
    }
};

/// Values over an `IndexBox` of a lattice, row by row.
template <typename T> struct LatticeWindow {
    IndexBox box;
    std::size_t width = 0;
    std::vector<T> values;

    /// The window over a non-empty `over`, every value `T{}`.
    explicit LatticeWindow(const IndexBox& over)
        : box(over), width(over.Columns()), values(over.Columns() * over.Rows()) {}

    T& At(std::int64_t col, std::int64_t row) {
        return values[static_cast<std::size_t>(row - box.row0) * width +
                      static_cast<std::size_t>(col - box.col0)];
    }
    const T& At(std::int64_t col, std::int64_t row) const {
        return values[static_cast<std::size_t>(row - box.row0) * width +
                      static_cast<std::size_t>(col - box.col0)];
    }
};

/// The posts of `posts` over `box`, NaN where they have no data.
LatticeWindow<float> ReadPosts(const PostLattice& posts, const IndexBox& box);

/// The index along an axis of the post a lattice read takes for index `at`: wrapped into
/// [0, `count`) when `wrap`, as longitudes around a body are, then clamped to [`begin`, `end`),
/// so reads beyond the posts held repeat their edge.
inline std::size_t PostIndex(std::int64_t at, bool wrap, std::size_t count, std::size_t begin,
                             std::size_t end) {
    const auto n = static_cast<std::int64_t>(count);
    const std::int64_t wrapped = wrap ? ((at % n) + n) % n : at;
    return static_cast<std::size_t>(std::clamp<std::int64_t>(
        wrapped, static_cast<std::int64_t>(begin), static_cast<std::int64_t>(end) - 1));
}

/// The `PostIndex` of each of the `size` indices from `first` along an axis.
inline std::vector<std::size_t> PostIndices(std::int64_t first, std::size_t size, bool wrap,
                                            std::size_t count, std::size_t begin, std::size_t end) {
    std::vector<std::size_t> indices(size);
    for (std::size_t i = 0; i < size; ++i) {
        indices[i] = PostIndex(first + static_cast<std::int64_t>(i), wrap, count, begin, end);
    }
    return indices;
}

/// Where a grid on a lattice starts, in lattice cells.
struct LatticeOffset {
    std::int64_t col = 0;
    std::int64_t row = 0;
};

/// The offset of a grid with transform `grid` on a lattice placed by `lattice`, if its cells are
/// the lattice's cells: the same size and rotation, starting a whole number of cells away.
std::optional<LatticeOffset> OffsetOnLattice(const GeoTransform& lattice, const GeoTransform& grid);

/// Cells per side of the tiles `FillByTiles` hands out.
inline constexpr std::size_t kFillTileCells = 128;

/// Sets the samples of `samples` by `fill(positions, values)`, which sets `values`, one per
/// position: a grid tile by tile, tiles split over threads, each tile's cell centers row by row;
/// points all at once.
/// @param threads Threads to fill a grid over; 0 for every hardware thread.
template <typename FillPoints>
void FillByTiles(SampleBlock& samples, unsigned threads, const FillPoints& fill) {
    Raster<float>* grid = samples.Grid();
    if (grid == nullptr) {
        fill(samples.Points(), samples.Heights());
        return;
    }
    Raster<float>& heights = *grid;
    ForEachTile(heights.Width(), heights.Height(), kFillTileCells, threads,
                [&](const PixelWindow& tile) {
                    const std::size_t width = tile.colEnd - tile.colBegin;
                    std::vector<Vec2d> centers;
                    centers.reserve(width * (tile.rowEnd - tile.rowBegin));
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        for (std::size_t col = tile.colBegin; col < tile.colEnd; ++col) {
                            centers.push_back(heights.CellCenter(col, row));
                        }
                    }
                    std::vector<float> values(centers.size());
                    fill(Span<const Vec2d>(centers), Span<float>(values));
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        std::copy_n(values.begin() +
                                        static_cast<std::ptrdiff_t>((row - tile.rowBegin) * width),
                                    width, heights.Row(row).begin() + tile.colBegin);
                    }
                });
}

/// Sets `heights`, one per point of `points`, by `expand(box, points, heights)`: once for them
/// all when the box `readBox(points)` gives holds at most `perPoint` indices per point, so points
/// close together read the lattice once, otherwise point by point with each point's own box.
template <typename ReadBox, typename Expand>
void ExpandPoints(Span<const Vec2d> points, Span<float> heights, double perPoint,
                  const ReadBox& readBox, const Expand& expand) {
    if (points.empty()) {
        return;
    }
    const IndexBox box = readBox(points);
    if (box.Area() <= perPoint * static_cast<double>(points.size())) {
        expand(box, points, heights);
        return;
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Span<const Vec2d> point = points.subspan(i, 1);
        expand(readBox(point), point, heights.subspan(i, 1));
    }
}

/// Evaluates the samples `pick` chooses all at once: `pick(i)` gives the position to evaluate
/// sample i of `count` at, or nothing to pass it by, in order; `evaluate(positions, values)` sets
/// one value per position chosen, and `place(i, value)` gives sample i its value.
template <typename Pick, typename Evaluate, typename Place>
void EvaluateChosen(std::size_t count, const Pick& pick, const Evaluate& evaluate,
                    const Place& place) {
    std::vector<Vec2d> chosen;
    std::vector<std::size_t> at;
    for (std::size_t i = 0; i < count; ++i) {
        if (const std::optional<Vec2d> position = pick(i)) {
            chosen.push_back(*position);
            at.push_back(i);
        }
    }
    if (chosen.empty()) {
        return;
    }
    std::vector<float> values(chosen.size());
    evaluate(Span<const Vec2d>(chosen), Span<float>(values));
    for (std::size_t k = 0; k < at.size(); ++k) {
        place(at[k], values[k]);
    }
}

/// The height `fill(samples)` gives a block of the one point (`x`, `y`).
template <typename Fill> float PointHeight(double x, double y, const Fill& fill) {
    const Vec2d point{x, y};
    float height = 0.0f;
    SampleBlock samples(Span<const Vec2d>(&point, 1), Span<float>(&height, 1));
    fill(samples);
    return height;
}

/// The grid of the cells `cells` of an unrotated `grid` (`Subgrid`) when it places their centers
/// where `grid` does to the bit, so a fill of it gives the bits a fill of `grid` gives them;
/// nothing otherwise, for a rotated grid or for no cells.
std::optional<RasterGrid> ExactSubgrid(const RasterGrid& grid, const PixelWindow& cells);

}  // namespace detail

/// Gives the NaN cells of `heights`, filled from a source with no-data value `noData`, that value,
/// and `heights` that no-data value, or NaN without one if any cell was missing.
void MarkMissing(Raster<float>& heights, std::optional<float> noData);

}  // namespace terraingen
