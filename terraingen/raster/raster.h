/// @file
/// @brief A single-band georeferenced grid of values.
#pragma once

#include "terraingen/raster/geo_transform.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace terraingen {

/// @brief A single-band, georeferenced grid of values stored row-major.
///
/// Row 0 is the first row in the file, the northern edge for north-up rasters. Cells are addressed
/// as (column, row), matching the (x, y) order of `GeoTransform`.
/// @tparam T Cell type, usually `float` heights in metres.
template <typename T> class Raster {
  public:
    /// @brief An empty raster with the identity transform and no no-data value.
    Raster() = default;

    /// @brief A raster with every cell set to one value.
    /// @param width Columns.
    /// @param height Rows.
    /// @param fill The value of every cell.
    Raster(std::size_t width, std::size_t height, T fill = T{})
        : width_(width), height_(height), values_(width * height, fill) {}

    /// @brief The number of columns.
    /// @return The width in cells.
    std::size_t Width() const {
        return width_;
    }

    /// @brief The number of rows.
    /// @return The height in cells.
    std::size_t Height() const {
        return height_;
    }

    /// @brief The number of cells.
    /// @return `Width() * Height()`.
    std::size_t Size() const {
        return values_.size();
    }

    /// @brief Whether the raster has no cells.
    /// @return True when `Size()` is 0.
    bool Empty() const {
        return values_.empty();
    }

    /// @brief Unchecked cell access; asserts in debug builds.
    /// @param col Column, below `Width()`.
    /// @param row Row, below `Height()`.
    /// @return The cell.
    T& operator()(std::size_t col, std::size_t row) {
        assert(col < width_ && row < height_);
        return values_[row * width_ + col];
    }

    /// @brief Unchecked read-only cell access; asserts in debug builds.
    /// @param col Column, below `Width()`.
    /// @param row Row, below `Height()`.
    /// @return The cell.
    const T& operator()(std::size_t col, std::size_t row) const {
        assert(col < width_ && row < height_);
        return values_[row * width_ + col];
    }

    /// @brief Bounds-checked cell access.
    /// @param col Column.
    /// @param row Row.
    /// @return The cell.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    T& At(std::size_t col, std::size_t row) {
        CheckBounds(col, row);
        return values_[row * width_ + col];
    }

    /// @brief Bounds-checked read-only cell access.
    /// @param col Column.
    /// @param row Row.
    /// @return The cell.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    const T& At(std::size_t col, std::size_t row) const {
        CheckBounds(col, row);
        return values_[row * width_ + col];
    }

    /// @brief All cells.
    /// @return The cells in row-major order.
    std::span<T> Values() {
        return values_;
    }

    /// @brief All cells, read-only.
    /// @return The cells in row-major order.
    std::span<const T> Values() const {
        return values_;
    }

    /// @brief The cells of one row; asserts in debug builds.
    /// @param row Row, below `Height()`.
    /// @return The row's cells, west to east for north-up rasters.
    std::span<T> Row(std::size_t row) {
        assert(row < height_);
        return std::span<T>(values_).subspan(row * width_, width_);
    }

    /// @brief The cells of one row, read-only; asserts in debug builds.
    /// @param row Row, below `Height()`.
    /// @return The row's cells, west to east for north-up rasters.
    std::span<const T> Row(std::size_t row) const {
        assert(row < height_);
        return std::span<const T>(values_).subspan(row * width_, width_);
    }

    /// @brief The pixel-to-world transform.
    /// @return The transform.
    const GeoTransform& Transform() const {
        return transform_;
    }

    /// @brief Sets the pixel-to-world transform.
    /// @param transform The new transform.
    void SetTransform(const GeoTransform& transform) {
        transform_ = transform;
    }

    /// @brief Whether a value is the no-data value.
    /// @param value The value to test.
    /// @return True when it equals the no-data value; a NaN no-data value matches every NaN. False
    /// without a no-data value.
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

    /// @brief The value marking cells without data.
    /// @return The value, if any.
    const std::optional<T>& NoData() const {
        return noData_;
    }

    /// @brief Sets the value marking cells without data.
    /// @param value The value, or none.
    void SetNoData(std::optional<T> value) {
        noData_ = value;
    }

    /// @brief The world coordinates of a cell's centre.
    /// @param col Column.
    /// @param row Row.
    /// @return The centre in world coordinates.
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
    std::vector<T> values_;
    GeoTransform transform_;
    std::optional<T> noData_;
};

}  // namespace terraingen
