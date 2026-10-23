// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Vec2d`, `Vec3d` and `Bounds2d` as Python tuples of floats.
#pragma once

#include "terraingen/util/geometry.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>

#include <array>
#include <cstddef>

namespace terraingen::python {

/// The components of a value, in the order Python sees them.
inline std::array<double, 2> Doubles(const Vec2d& v) {
    return {v.x, v.y};
}

inline std::array<double, 3> Doubles(const Vec3d& v) {
    return {v.x, v.y, v.z};
}

inline std::array<double, 4> Doubles(const Bounds2d& b) {
    return {b.minX, b.minY, b.maxX, b.maxY};
}

/// The value whose `Doubles` are `a`.
template <typename T, std::size_t N> T FromDoubles(const std::array<double, N>& a) {
    if constexpr (N == 2) {
        return T{a[0], a[1]};
    } else if constexpr (N == 3) {
        return T{a[0], a[1], a[2]};
    } else {
        return T{a[0], a[1], a[2], a[3]};
    }
}

}  // namespace terraingen::python

namespace nanobind::detail {

/// Converts `T` from any sequence of numbers and to a tuple of floats.
template <typename T> struct doubles_caster {
    NB_TYPE_CASTER(T, const_name("tuple"))

    using Array = decltype(terraingen::python::Doubles(T{}));

    bool from_python(handle src, uint32_t flags, cleanup_list* cleanup) noexcept {
        make_caster<Array> caster;
        if (!caster.from_python(src, flags, cleanup)) {
            return false;
        }
        value = terraingen::python::FromDoubles<T>(caster.operator cast_t<Array>());
        return true;
    }

    static handle from_cpp(const T& v, rv_policy /*policy*/, cleanup_list* /*cleanup*/) noexcept {
        const Array values = terraingen::python::Doubles(v);
        PyObject* tuple = PyTuple_New(static_cast<Py_ssize_t>(values.size()));
        for (std::size_t i = 0; tuple != nullptr && i < values.size(); ++i) {
            PyObject* item = PyFloat_FromDouble(values[i]);
            if (item == nullptr || PyTuple_SetItem(tuple, static_cast<Py_ssize_t>(i), item) != 0) {
                Py_DECREF(tuple);
                return {};
            }
        }
        return tuple;
    }
};

template <> struct type_caster<terraingen::Vec2d> : doubles_caster<terraingen::Vec2d> {
    static constexpr auto Name = const_name("tuple[float, float]");
};

template <> struct type_caster<terraingen::Vec3d> : doubles_caster<terraingen::Vec3d> {
    static constexpr auto Name = const_name("tuple[float, float, float]");
};

template <> struct type_caster<terraingen::Bounds2d> : doubles_caster<terraingen::Bounds2d> {
    static constexpr auto Name = const_name("tuple[float, float, float, float]");
};

}  // namespace nanobind::detail
