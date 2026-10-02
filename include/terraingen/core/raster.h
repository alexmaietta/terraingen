#pragma once

#include "terraingen/core/geo_transform.h"

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

/// A single-band, georeferenced grid of values stored row-major.
///
/// Row 0 is the first row in the file, which is the northern edge for north-up rasters. Values are
/// addressed as (column, row), matching the (x, y) order of `GeoTransform`.
///
/// @tparam T Cell type, usually `float` heights in metres.
template <typename T> class Raster {
  public:
    Raster() = default;

    /// Creates a `width` x `height` raster with every cell set to `fill`.
    Raster(std::size_t width, std::size_t height, T fill = T{})
        : width_(width), height_(height), values_(width * height, fill) {}

    std::size_t Width() const {
        return width_;
    }

    std::size_t Height() const {
        return height_;
    }

    /// Number of cells, `Width() * Height()`.
    std::size_t Size() const {
        return values_.size();
    }

    bool Empty() const {
        return values_.empty();
    }

    /// Unchecked cell access; asserts in debug builds.
    T& operator()(std::size_t col, std::size_t row) {
        assert(col < width_ && row < height_);
        return values_[row * width_ + col];
    }

    const T& operator()(std::size_t col, std::size_t row) const {
        assert(col < width_ && row < height_);
        return values_[row * width_ + col];
    }

    /// Bounds-checked cell access.
    /// @throws std::out_of_range if `col` or `row` is outside the raster.
    T& At(std::size_t col, std::size_t row) {
        CheckBounds(col, row);
        return values_[row * width_ + col];
    }

    const T& At(std::size_t col, std::size_t row) const {
        CheckBounds(col, row);
        return values_[row * width_ + col];
    }

    /// All cells in row-major order.
    std::span<T> Values() {
        return values_;
    }

    std::span<const T> Values() const {
        return values_;
    }

    /// Cells of one row, west to east for north-up rasters.
    std::span<T> Row(std::size_t row) {
        assert(row < height_);
        return std::span<T>(values_).subspan(row * width_, width_);
    }

    std::span<const T> Row(std::size_t row) const {
        assert(row < height_);
        return std::span<const T>(values_).subspan(row * width_, width_);
    }

    const GeoTransform& Transform() const {
        return transform_;
    }

    void SetTransform(const GeoTransform& transform) {
        transform_ = transform;
    }

    /// Coordinate reference system as WKT, or empty if unknown.
    const std::string& Crs() const {
        return crs_;
    }

    void SetCrs(std::string wkt) {
        crs_ = std::move(wkt);
    }

    /// Value marking cells without data, if any.
    const std::optional<T>& NoData() const {
        return noData_;
    }

    void SetNoData(std::optional<T> value) {
        noData_ = value;
    }

    /// World coordinates of a cell's centre.
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
    std::string crs_;
    std::optional<T> noData_;
};

}  // namespace terraingen
