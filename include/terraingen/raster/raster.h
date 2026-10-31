// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A single-band georeferenced grid of values.
#pragma once

#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/span.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace terraingen {

/// A single-band, georeferenced grid of values stored row-major, in cells of its own or in a
/// caller's buffer it borrows.
///
/// Row 0 is the first row in the file, the northern edge for north-up rasters. Cells are
/// addressed as (column, row), matching the (x, y) order of `GeoTransform`. A copy always owns
/// its cells; a move keeps borrowing what the raster borrowed.
/// @tparam T Cell type, usually `float` heights in meters.
template <typename T> class Raster {
  public:
    /// An empty raster with the identity transform and no no-data value.
    Raster() = default;

    /// A raster of `width` columns and `height` rows with the identity transform, every cell
    /// set to `fill`.
    Raster(std::size_t width, std::size_t height, T fill = T{})
        : Raster(RasterGrid{width, height, GeoTransform{}}, fill) {}

    /// A raster over `grid`, every cell set to `fill`.
    explicit Raster(const RasterGrid& grid, T fill = T{})
        : m_grid(grid), m_owned(grid.width * grid.height, fill), m_cells(m_owned.data()) {}

    /// A raster over `grid` whose cells are `cells`, which must outlive it; writes go to
    /// `cells`.
    /// @throws std::invalid_argument unless `cells` holds one value per cell of `grid`.
    Raster(const RasterGrid& grid, Span<T> cells) : m_grid(grid), m_cells(cells.data()) {
        if (cells.size() != grid.width * grid.height) {
            throw std::invalid_argument("A raster borrowing cells needs one value per cell");
        }
    }

    Raster(const Raster& other)
        : m_grid(other.m_grid), m_owned(other.m_cells, other.m_cells + other.Size()),
          m_cells(m_owned.data()), m_noData(other.m_noData) {}

    Raster(Raster&& other) noexcept
        : m_grid(other.m_grid), m_owned(std::move(other.m_owned)), m_cells(other.m_cells),
          m_noData(other.m_noData) {
        other.m_grid.width = 0;
        other.m_grid.height = 0;
        other.m_cells = nullptr;
    }

    Raster& operator=(Raster other) noexcept {
        std::swap(m_grid, other.m_grid);
        std::swap(m_owned, other.m_owned);
        std::swap(m_cells, other.m_cells);
        std::swap(m_noData, other.m_noData);
        return *this;
    }

    ~Raster() = default;

    /// The raster's size and pixel-to-world transform.
    const RasterGrid& Grid() const {
        return m_grid;
    }

    /// The number of columns.
    std::size_t Width() const {
        return m_grid.width;
    }

    /// The number of rows.
    std::size_t Height() const {
        return m_grid.height;
    }

    /// The number of cells, `Width() * Height()`.
    std::size_t Size() const {
        return m_grid.width * m_grid.height;
    }

    /// Whether the raster has no cells.
    bool Empty() const {
        return Size() == 0;
    }

    /// Unchecked cell access; asserts `col < Width()` and `row < Height()` in debug builds.
    T& operator()(std::size_t col, std::size_t row) {
        assert(col < m_grid.width && row < m_grid.height);
        return m_cells[row * m_grid.width + col];
    }

    /// Unchecked read-only cell access; asserts in debug builds.
    const T& operator()(std::size_t col, std::size_t row) const {
        assert(col < m_grid.width && row < m_grid.height);
        return m_cells[row * m_grid.width + col];
    }

    /// Bounds-checked cell access.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    T& At(std::size_t col, std::size_t row) {
        CheckBounds(col, row);
        return m_cells[row * m_grid.width + col];
    }

    /// Bounds-checked read-only cell access.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    const T& At(std::size_t col, std::size_t row) const {
        CheckBounds(col, row);
        return m_cells[row * m_grid.width + col];
    }

    /// All cells in row-major order.
    Span<T> Values() {
        return {m_cells, Size()};
    }

    /// All cells in row-major order, read-only.
    Span<const T> Values() const {
        return {m_cells, Size()};
    }

    /// The cells of one row, west to east for north-up rasters; asserts in debug builds.
    Span<T> Row(std::size_t row) {
        assert(row < m_grid.height);
        return {m_cells + row * m_grid.width, m_grid.width};
    }

    /// The cells of one row, read-only; asserts in debug builds.
    Span<const T> Row(std::size_t row) const {
        assert(row < m_grid.height);
        return {m_cells + row * m_grid.width, m_grid.width};
    }

    /// The pixel-to-world transform.
    const GeoTransform& Transform() const {
        return m_grid.transform;
    }

    /// Whether `value` is the no-data value; a NaN no-data value matches every NaN, and nothing
    /// matches without a no-data value.
    bool IsNoData(const T& value) const {
        if (!m_noData) {
            return false;
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isnan(*m_noData)) {
                return std::isnan(value);
            }
        }
        return value == *m_noData;
    }

    /// Whether `value` holds no data: not finite, or the no-data value.
    bool IsMissing(const T& value) const {
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(value)) {
                return true;
            }
        }
        return IsNoData(value);
    }

    /// The value marking cells without data, if any.
    const std::optional<T>& NoData() const {
        return m_noData;
    }

    /// Sets the value marking cells without data, or none.
    void SetNoData(std::optional<T> value) {
        m_noData = value;
    }

    /// The world coordinates of a cell's center.
    Vec2d CellCenter(std::size_t col, std::size_t row) const {
        return m_grid.transform.PixelToWorld(static_cast<double>(col) + 0.5,
                                             static_cast<double>(row) + 0.5);
    }

  private:
    void CheckBounds(std::size_t col, std::size_t row) const {
        if (col >= m_grid.width || row >= m_grid.height) {
            throw std::out_of_range("Raster cell out of range");
        }
    }

    RasterGrid m_grid;
    std::vector<T> m_owned;
    T* m_cells = nullptr;
    std::optional<T> m_noData;
};

}  // namespace terraingen
