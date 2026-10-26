// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A single-band georeferenced grid of values.
#pragma once

#include "terraingen/raster/geo_transform.h"
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
/// Row 0 is the first row in the file, the northern edge for north-up rasters. Cells are addressed
/// as (column, row), matching the (x, y) order of `GeoTransform`. A copy always owns its cells; a
/// move keeps borrowing what the raster borrowed.
/// @tparam T Cell type, usually `float` heights in meters.
template <typename T> class Raster {
  public:
    /// An empty raster with the identity transform and no no-data value.
    Raster() = default;

    /// A raster of `width` columns and `height` rows, every cell set to `fill`.
    Raster(std::size_t width, std::size_t height, T fill = T{})
        : width_(width), height_(height), owned_(width * height, fill), cells_(owned_.data()) {}

    /// A raster of `width` columns and `height` rows whose cells are `cells`, which must outlive
    /// it; writes go to `cells`.
    /// @throws std::invalid_argument unless `cells` holds `width * height` values.
    Raster(std::size_t width, std::size_t height, Span<T> cells)
        : width_(width), height_(height), cells_(cells.data()) {
        if (cells.size() != width * height) {
            throw std::invalid_argument("A raster borrowing cells needs one value per cell");
        }
    }

    Raster(const Raster& other)
        : width_(other.width_), height_(other.height_),
          owned_(other.cells_, other.cells_ + other.Size()), cells_(owned_.data()),
          transform_(other.transform_), noData_(other.noData_) {}

    Raster(Raster&& other) noexcept
        : width_(other.width_), height_(other.height_), owned_(std::move(other.owned_)),
          cells_(other.cells_), transform_(other.transform_), noData_(other.noData_) {
        other.width_ = 0;
        other.height_ = 0;
        other.cells_ = nullptr;
    }

    Raster& operator=(Raster other) noexcept {
        std::swap(width_, other.width_);
        std::swap(height_, other.height_);
        std::swap(owned_, other.owned_);
        std::swap(cells_, other.cells_);
        std::swap(transform_, other.transform_);
        std::swap(noData_, other.noData_);
        return *this;
    }

    ~Raster() = default;

    /// The number of columns.
    std::size_t Width() const {
        return width_;
    }

    /// The number of rows.
    std::size_t Height() const {
        return height_;
    }

    /// The number of cells, `Width() * Height()`.
    std::size_t Size() const {
        return width_ * height_;
    }

    /// Whether the raster has no cells.
    bool Empty() const {
        return Size() == 0;
    }

    /// Unchecked cell access; asserts `col < Width()` and `row < Height()` in debug builds.
    T& operator()(std::size_t col, std::size_t row) {
        assert(col < width_ && row < height_);
        return cells_[row * width_ + col];
    }

    /// Unchecked read-only cell access; asserts in debug builds.
    const T& operator()(std::size_t col, std::size_t row) const {
        assert(col < width_ && row < height_);
        return cells_[row * width_ + col];
    }

    /// Bounds-checked cell access.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    T& At(std::size_t col, std::size_t row) {
        CheckBounds(col, row);
        return cells_[row * width_ + col];
    }

    /// Bounds-checked read-only cell access.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    const T& At(std::size_t col, std::size_t row) const {
        CheckBounds(col, row);
        return cells_[row * width_ + col];
    }

    /// All cells in row-major order.
    Span<T> Values() {
        return {cells_, Size()};
    }

    /// All cells in row-major order, read-only.
    Span<const T> Values() const {
        return {cells_, Size()};
    }

    /// The cells of one row, west to east for north-up rasters; asserts in debug builds.
    Span<T> Row(std::size_t row) {
        assert(row < height_);
        return {cells_ + row * width_, width_};
    }

    /// The cells of one row, read-only; asserts in debug builds.
    Span<const T> Row(std::size_t row) const {
        assert(row < height_);
        return {cells_ + row * width_, width_};
    }

    /// Whether the cells are a caller's buffer rather than the raster's own.
    bool Borrows() const {
        return cells_ != nullptr && owned_.empty();
    }

    /// The pixel-to-world transform.
    const GeoTransform& Transform() const {
        return transform_;
    }

    /// Sets the pixel-to-world transform.
    void SetTransform(const GeoTransform& transform) {
        transform_ = transform;
    }

    /// Whether `value` is the no-data value; a NaN no-data value matches every NaN, and nothing
    /// matches without a no-data value.
    bool IsNoData(const T& value) const {
        if (!noData_) {
            return false;
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isnan(*noData_)) {
                return std::isnan(value);
            }
        }
        return value == *noData_;
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
        return noData_;
    }

    /// Sets the value marking cells without data, or none.
    void SetNoData(std::optional<T> value) {
        noData_ = value;
    }

    /// The world coordinates of a cell's center.
    Vec2d CellCenter(std::size_t col, std::size_t row) const {
        return transform_.PixelToWorld(static_cast<double>(col) + 0.5,
                                       static_cast<double>(row) + 0.5);
    }

  private:
    void CheckBounds(std::size_t col, std::size_t row) const {
        if (col >= width_ || row >= height_) {
            throw std::out_of_range("Raster cell out of range");
        }
    }

    std::size_t width_ = 0;
    std::size_t height_ = 0;
    std::vector<T> owned_;
    T* cells_ = nullptr;
    GeoTransform transform_;
    std::optional<T> noData_;
};

}  // namespace terraingen
