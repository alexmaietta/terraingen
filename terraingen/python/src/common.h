/// @file
/// @brief What every binding file shares: the nanobind alias, the tuple casters and helpers for
/// option structs.
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

/// @brief Binds `__init__(**kwargs)` for an option struct: the defaults, then each keyword set
/// as the attribute of that name, so `BoulderLayerOptions(coverage=0.05)` reads like its config.
/// @param cls The class being bound.
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

/// @brief Binds `name` as an attribute of `Outer` that reads and writes `(outer.*group).*field`,
/// so options grouped into structs in C++ keep the flat attribute names of their config.
/// @param cls The class being bound.
/// @param name The Python attribute name.
/// @param group The member holding the group.
/// @param field The group's member.
template <typename Outer, typename Group, typename T, typename Class>
void BindGrouped(Class& cls, const char* name, Group Outer::* group, T Group::* field) {
    cls.def_prop_rw(
        name, [group, field](Outer& outer) -> T& { return (outer.*group).*field; },
        [group, field](Outer& outer, const T& value) { (outer.*group).*field = value; });
}

/// @brief Binds the fields of an options struct's `CraterSelection` under their flat config
/// names: the diameter bounds as `minDiameter` and `maxDiameter`, then `min_age_gyr`,
/// `max_age_gyr` and `manual`.
/// @param cls The class being bound.
/// @param selection The member holding the selection.
/// @param minDiameter The Python name of the smallest diameter.
/// @param maxDiameter The Python name of the largest diameter.
template <typename Outer, typename Selection, typename Class>
void BindSelection(Class& cls, Selection Outer::* selection, const char* minDiameter,
                   const char* maxDiameter) {
    BindGrouped(cls, minDiameter, selection, &Selection::minDiameter);
    BindGrouped(cls, maxDiameter, selection, &Selection::maxDiameter);
    BindGrouped(cls, "min_age_gyr", selection, &Selection::minAgeGyr);
    BindGrouped(cls, "max_age_gyr", selection, &Selection::maxAgeGyr);
    BindGrouped(cls, "manual", selection, &Selection::manual);
}

/// @brief A finite or infinite upper bound from an optional value.
/// @param value The bound, or None for no bound.
/// @return `value`, or infinity without one.
inline double Bound(const std::optional<double>& value) {
    return value.value_or(std::numeric_limits<double>::infinity());
}

/// @brief A shared pointer Python can hold: Python objects are never const.
/// @param pointer The library's pointer.
/// @return The same object, without const.
template <typename T> std::shared_ptr<T> Mutable(const std::shared_ptr<const T>& pointer) {
    return std::const_pointer_cast<T>(pointer);
}

/// @brief The docstring of every `validate_*` function.
inline constexpr const char* kValidateDoc = "Raises ValueError naming the first invalid setting.";

}  // namespace terraingen::python
