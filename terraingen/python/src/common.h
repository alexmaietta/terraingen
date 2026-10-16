/// @file
/// @brief What every binding file shares: the nanobind alias, casters and option helpers.
#pragma once

#include "casters.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <limits>
#include <memory>
#include <optional>

namespace terraingen::python {

namespace nb = nanobind;
using namespace nb::literals;

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

/// Binds `name` as an attribute of `Outer` that reads and writes `(outer.*group).*field`,
/// so options grouped into structs in C++ keep the flat attribute names of their config.
template <typename Outer, typename Group, typename T, typename Class>
void BindGrouped(Class& cls, const char* name, Group Outer::* group, T Group::* field) {
    cls.def_prop_rw(
        name, [group, field](Outer& outer) -> T& { return (outer.*group).*field; },
        [group, field](Outer& outer, const T& value) { (outer.*group).*field = value; });
}

/// Binds the fields of an options struct's `CraterSelection` under their flat config
/// names: the diameter bounds as `minDiameter` and `maxDiameter`, then `min_age_gyr`,
/// `max_age_gyr` and `manual`.
template <typename Outer, typename Selection, typename Class>
void BindSelection(Class& cls, Selection Outer::* selection, const char* minDiameter,
                   const char* maxDiameter) {
    BindGrouped(cls, minDiameter, selection, &Selection::minDiameter);
    BindGrouped(cls, maxDiameter, selection, &Selection::maxDiameter);
    BindGrouped(cls, "min_age_gyr", selection, &Selection::minAgeGyr);
    BindGrouped(cls, "max_age_gyr", selection, &Selection::maxAgeGyr);
    BindGrouped(cls, "manual", selection, &Selection::manual);
}

/// An upper bound from an optional value: `value`, or infinity for None.
inline double Bound(const std::optional<double>& value) {
    return value.value_or(std::numeric_limits<double>::infinity());
}

/// `pointer` without const, since Python objects are never const.
template <typename T> std::shared_ptr<T> Mutable(const std::shared_ptr<const T>& pointer) {
    return std::const_pointer_cast<T>(pointer);
}

/// The docstring of every `validate_*` function.
inline constexpr const char* kValidateDoc = "Raises ValueError naming the first invalid setting.";

}  // namespace terraingen::python
