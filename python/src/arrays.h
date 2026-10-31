// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The NumPy arrays the bindings take and return.
#pragma once

#include "common.h"

#include "terraingen/geometry/geometry.h"
#include "terraingen/util/span.h"

#include <nanobind/ndarray.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::python {

/// A C-contiguous array of `T` in CPU memory, of any shape.
template <typename T> using CpuArray = nb::ndarray<const T, nb::device::cpu, nb::c_contig>;

/// A C-contiguous one-dimensional array of `T` in CPU memory.
template <typename T>
using CpuColumn = nb::ndarray<const T, nb::ndim<1>, nb::device::cpu, nb::c_contig>;

/// A C-contiguous two-dimensional array of `T` in CPU memory, rows first.
template <typename T>
using CpuMatrix = nb::ndarray<const T, nb::ndim<2>, nb::device::cpu, nb::c_contig>;

/// A writable C-contiguous two-dimensional array of `T` in CPU memory, rows first, that the
/// bindings fill in place.
template <typename T>
using CpuOutMatrix = nb::ndarray<T, nb::ndim<2>, nb::device::cpu, nb::c_contig>;

/// A NumPy array of `T` returned to Python.
template <typename T> using NumpyArray = nb::ndarray<nb::numpy, T>;

/// A NumPy array of `shape` owning `values`, whose size is the product of `shape`.
template <typename T>
NumpyArray<T> OwnedArray(std::vector<T> values, const std::vector<std::size_t>& shape) {
    auto* owned = new std::vector<T>(std::move(values));
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<std::vector<T>*>(p); });
    return NumpyArray<T>(owned->data(), shape.size(), shape.data(), owner);
}

/// A one-dimensional NumPy array owning `values`, as a Python object.
template <typename T> nb::object Column(std::vector<T> values) {
    const std::size_t size = values.size();
    return nb::cast(OwnedArray(std::move(values), {size}));
}

/// The rows of an array of shape (n, `N`).
/// @throws std::invalid_argument for another number of columns.
template <std::size_t N, typename T> std::vector<std::array<T, N>> Rows(const CpuMatrix<T>& array) {
    if (array.shape(1) != N) {
        throw std::invalid_argument("Expected an array of shape (n, " + std::to_string(N) + ")");
    }
    std::vector<std::array<T, N>> rows(array.shape(0));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (std::size_t j = 0; j < N; ++j) {
            rows[i][j] = array.data()[N * i + j];
        }
    }
    return rows;
}

/// The rows of `points`, an array of shape (n, 2), as points, without a copy.
/// @throws std::invalid_argument for another number of columns.
inline Span<const Vec2d> PointSpan(const CpuMatrix<double>& points) {
    static_assert(sizeof(Vec2d) == 2 * sizeof(double), "Vec2d is two packed doubles");
    if (points.shape(1) != 2) {
        throw std::invalid_argument("Expected an array of shape (n, 2)");
    }
    return {reinterpret_cast<const Vec2d*>(points.data()), points.shape(0)};
}

/// The shape of `array`.
template <typename Array> std::vector<std::size_t> Shape(const Array& array) {
    std::vector<std::size_t> shape(array.ndim());
    for (std::size_t i = 0; i < shape.size(); ++i) {
        shape[i] = array.shape(i);
    }
    return shape;
}

}  // namespace terraingen::python
