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

#include <memory>

namespace terraingen::python {

namespace nb = nanobind;
using namespace nb::literals;

/// Binds `__init__(**kwargs)` for an option struct: the defaults, then each keyword set as the
/// attribute of that name, so `CraterLayerOptions(seed=4)` reads like its config.
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

/// `pointer` without const, since Python objects are never const.
template <typename T> std::shared_ptr<T> Mutable(const std::shared_ptr<const T>& pointer) {
    return std::const_pointer_cast<T>(pointer);
}

/// The docstring of every `validate_*` function and `validate()` method.
inline constexpr const char* kValidateDoc = "Raises ValueError naming the first invalid setting.";

}  // namespace terraingen::python
