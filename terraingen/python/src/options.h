#pragma once

#include "terraingen/util/geometry.h"

#include <nanobind/nanobind.h>

#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace terraingen::python {

namespace nb = nanobind;

/// Binds `__init__(**kwargs)` for an option struct: the defaults, then each keyword set as the
/// attribute of that name, so `BoulderLayerOptions(coverage=0.05)` reads like its config.
template <typename T, typename Class> void BindKwargsInit(Class& cls) {
    cls.def(
        "__init__",
        [](T* self, nb::kwargs kwargs) {
            new (self) T{};
            const nb::handle instance = nb::find(self);
            nb::inst_mark_ready(instance);
            for (auto [key, value] : kwargs) {
                nb::setattr(instance, key, value);
            }
        },
        "Defaults, with each keyword setting the attribute of that name.");
}

/// Binds `name` as an attribute of `Outer` that reads and writes `(outer.*group).*field`, so
/// options grouped into structs in C++ keep the flat attribute names of their config.
template <typename Outer, typename Group, typename T, typename Class>
void BindGrouped(Class& cls, const char* name, Group Outer::* group, T Group::* field) {
    cls.def_prop_rw(
        name, [group, field](Outer& outer) -> T& { return (outer.*group).*field; },
        [group, field](Outer& outer, const T& value) { (outer.*group).*field = value; });
}

/// Binds the fields of an options struct's `CraterSelection` under their flat config names: the
/// diameter bounds as `minDiameter` and `maxDiameter`, then `min_age_gyr`, `max_age_gyr` and
/// `manual`.
template <typename Outer, typename Selection, typename Class>
void BindSelection(Class& cls, Selection Outer::* selection, const char* minDiameter,
                   const char* maxDiameter) {
    BindGrouped(cls, minDiameter, selection, &Selection::minDiameter);
    BindGrouped(cls, maxDiameter, selection, &Selection::maxDiameter);
    BindGrouped(cls, "min_age_gyr", selection, &Selection::minAgeGyr);
    BindGrouped(cls, "max_age_gyr", selection, &Selection::maxAgeGyr);
    BindGrouped(cls, "manual", selection, &Selection::manual);
}

/// A finite or infinite upper bound from an optional value: None for no bound.
inline double Bound(const std::optional<double>& value) {
    return value.value_or(std::numeric_limits<double>::infinity());
}

/// An unbounded maximum as None.
inline std::optional<double> Unbounded(double value) {
    if (value == std::numeric_limits<double>::infinity()) {
        return std::nullopt;
    }
    return value;
}

/// (min_x, min_y, max_x, max_y) as bounds.
inline Bounds2d BoundsOf(const std::tuple<double, double, double, double>& b) {
    return {std::get<0>(b), std::get<1>(b), std::get<2>(b), std::get<3>(b)};
}

}  // namespace terraingen::python
