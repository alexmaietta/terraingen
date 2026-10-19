/// @file
/// @brief Calling the methods a Python subclass overrides, for the trampolines.
#pragma once

#include "common.h"

#include <nanobind/trampoline.h>

#include <optional>
#include <string>
#include <utility>

namespace terraingen::python {

/// An argument passed to Python by reference rather than copied, such as a raster the
/// override changes in place.
template <typename T> struct ByReference {
    T& value;
};

template <typename T> decltype(auto) PythonArgument(T&& argument) {
    return std::forward<T>(argument);
}

template <typename T> nb::object PythonArgument(ByReference<T> argument) {
    return nb::cast(&argument.value, nb::rv_policy::reference);
}

/// Calls the Python override `name` if the subclass defines one. The ticket holds the GIL
/// while the arguments are wrapped and the result converted.
/// @param args The arguments; wrap ones to pass by reference in `ByReference`.
/// @return What the override returned as `Result`, or nothing without an override.
template <typename Result, typename... Args>
std::optional<Result> CallOverride(const nb::detail::trampoline& trampoline, const char* name,
                                   Args&&... args) {
    const nb::detail::ticket ticket(trampoline, name, nb::detail::str_hash(name), false);
    if (!ticket.key.is_valid()) {
        return std::nullopt;
    }
    return nb::cast<Result>(
        trampoline.base().attr(ticket.key)(PythonArgument(std::forward<Args>(args))...));
}

/// Calls the Python override `name`, ignoring its result, if the subclass defines one, and
/// returns whether it did.
template <typename... Args>
bool CallOverrideIgnoringResult(const nb::detail::trampoline& trampoline, const char* name,
                                Args&&... args) {
    const nb::detail::ticket ticket(trampoline, name, nb::detail::str_hash(name), false);
    if (!ticket.key.is_valid()) {
        return false;
    }
    trampoline.base().attr(ticket.key)(PythonArgument(std::forward<Args>(args))...);
    return true;
}

/// Calls the Python method `name` the subclass must define and converts its result with
/// `convert`, while the GIL is held.
/// @throws nb::type_error (TypeError in Python) when the subclass does not define it.
template <typename Convert, typename... Args>
auto CallRequired(const nb::detail::trampoline& trampoline, const char* name, Convert convert,
                  Args&&... args) {
    const nb::detail::ticket ticket(trampoline, name, nb::detail::str_hash(name), false);
    if (!ticket.key.is_valid()) {
        const std::string type = nb::cast<std::string>(trampoline.base().type().attr("__name__"));
        throw nb::type_error((type + " must define " + name + "()").c_str());
    }
    return convert(trampoline.base().attr(ticket.key)(PythonArgument(std::forward<Args>(args))...));
}

/// `CallRequired` with the result converted to `Result`.
template <typename Result, typename... Args>
Result CallRequiredAs(const nb::detail::trampoline& trampoline, const char* name, Args&&... args) {
    return CallRequired(
        trampoline, name, [](nb::handle result) { return nb::cast<Result>(result); },
        std::forward<Args>(args)...);
}

/// Whether the Python subclass behind `trampoline` overrides `name`.
inline bool Overrides(const nb::detail::trampoline& trampoline, const char* name) {
    const nb::detail::ticket ticket(trampoline, name, nb::detail::str_hash(name), false);
    return ticket.key.is_valid();
}

}  // namespace terraingen::python
