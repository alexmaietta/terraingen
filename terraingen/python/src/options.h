/// @file
/// @brief Option structs and enums bound from the configuration's field lists and enum names.
#pragma once

#include "common.h"

#include "terraingen/config/fields/enums.h"
#include "terraingen/config/fields/fields.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen::python {

/// Walks a struct's configuration field list, `config::Fields`.
struct VisitFields {
    template <typename IO, typename T> void operator()(IO& io, T& value) const {
        config::Fields(io, value);
    }
};

/// The docstring of a field: its description, then its unit when it has one.
inline std::string FieldDoc(std::string_view description, std::string_view unit) {
    std::string doc(description);
    if (!unit.empty()) {
        doc += " Unit: " + std::string(unit) + ".";
    }
    return doc;
}

/// The field-list `IO` that binds each field of an options struct `T` as a property of its class,
/// found by its offset in a default `T`: a bound unset when infinite as an optional float (None
/// when unset), and a group of fields the configuration writes flat as one attribute.
template <typename T> class OptionsBinder {
  public:
    explicit OptionsBinder(nb::class_<T>& cls) : cls_(cls) {}

    /// Binds `member` as the property `key`.
    template <typename F>
    void operator()(std::string_view key, F& member, std::string_view description = {},
                    std::string_view unit = {}, const config::FieldRange& /*range*/ = {}) {
        using Q = std::conditional_t<nb::detail::is_base_caster_v<nb::detail::make_caster<F>>,
                                     const F&, F&&>;
        const std::size_t offset = Offset(key, member);
        Add(key);
        cls_.def_prop_rw(
            names_->back().c_str(),
            [offset](const T& self) -> const F& { return At<F>(self, offset); },
            [offset](T& self, Q value) { At<F>(self, offset) = static_cast<Q>(value); },
            FieldDoc(description, unit).c_str());
    }

    /// Binds `bound` as the property `key`: None when it is not finite, and `unset` when set to
    /// None.
    void Bound(std::string_view key, double& bound, std::string_view description,
               std::string_view unit, double unset, const config::FieldRange& /*range*/) {
        const std::size_t offset = Offset(key, bound);
        Add(key);
        cls_.def_prop_rw(
            names_->back().c_str(),
            [offset](const T& self) -> std::optional<double> {
                const double value = At<double>(self, offset);
                return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
            },
            [offset, unset](T& self, std::optional<double> value) {
                At<double>(self, offset) = value.value_or(unset);
            },
            FieldDoc(description, unit).c_str());
    }

    /// Binds `group` as the property `name`, or returns false to have its fields bound one by one
    /// when it is the whole struct.
    template <typename G>
    bool Group(std::string_view name, G& group, std::string_view description) {
        if (static_cast<const void*>(&group) == static_cast<const void*>(&prototype_) &&
            sizeof(G) == sizeof(T)) {
            return false;
        }
        (*this)(name, group, description);
        return true;
    }

    /// The default `T` whose members the field list visits.
    T& Prototype() {
        return prototype_;
    }

    /// The names of the properties bound so far, in order.
    std::shared_ptr<const std::vector<std::string>> Names() const {
        return names_;
    }

  private:
    template <typename F> static F& At(T& self, std::size_t offset) {
        return *reinterpret_cast<F*>(reinterpret_cast<char*>(&self) + offset);
    }

    template <typename F> static const F& At(const T& self, std::size_t offset) {
        return *reinterpret_cast<const F*>(reinterpret_cast<const char*>(&self) + offset);
    }

    /// The offset of `member` in `T`.
    /// @throws std::logic_error when the field list passes a value that is not a member.
    template <typename F> std::size_t Offset(std::string_view key, const F& member) const {
        const auto* base = reinterpret_cast<const char*>(&prototype_);
        const auto* field = reinterpret_cast<const char*>(&member);
        if (field < base || field + sizeof(F) > base + sizeof(T)) {
            throw std::logic_error("The field " + std::string(key) + " is not a member");
        }
        return static_cast<std::size_t>(field - base);
    }

    void Add(std::string_view key) {
        names_->emplace_back(key);
    }

    nb::class_<T>& cls_;
    T prototype_{};
    std::shared_ptr<std::vector<std::string>> names_ = std::make_shared<std::vector<std::string>>();
};

/// Binds `__init__(*args, **kwargs)` for an options struct whose fields are `names`: the
/// defaults, then each positional argument set as the field of its place and each keyword as the
/// field of that name.
template <typename T>
void BindFieldsInit(nb::class_<T>& cls, std::shared_ptr<const std::vector<std::string>> names) {
    cls.def(
        "__init__",
        [names](T* self, nb::args args, nb::kwargs kwargs) {
            if (args.size() > names->size()) {
                throw nb::type_error("Too many positional arguments");
            }
            new (self) T{};
            const nb::handle instance = nb::find(self);
            nb::inst_mark_ready(instance);
            for (std::size_t i = 0; i < args.size(); ++i) {
                nb::setattr(instance, (*names)[i].c_str(), args[i]);
            }
            for (auto [key, value] : kwargs) {
                nb::setattr(instance, key, value);
            }
        },
        "Defaults, with positional arguments setting the fields in order and each keyword the "
        "field of that name.");
}

/// Binds `__repr__` as "Name(field=value, ...)" over the fields `names`.
template <typename T>
void BindFieldsRepr(nb::class_<T>& cls, std::shared_ptr<const std::vector<std::string>> names) {
    cls.def("__repr__", [names](nb::handle self) {
        std::string text = nb::cast<std::string>(self.type().attr("__name__")) + "(";
        for (std::size_t i = 0; i < names->size(); ++i) {
            const char* name = (*names)[i].c_str();
            text += (i == 0 ? "" : ", ") + std::string(name) + "=" +
                    nb::cast<std::string>(nb::repr(nb::getattr(self, name)));
        }
        return text + ")";
    });
}

/// Binds the options struct `T` as the class `name`, its fields from `visit(io, value)` (its
/// configuration field list by default) as properties documented by their descriptions and units,
/// with a constructor taking the fields and a `__repr__` listing them.
template <typename T, typename Visit = VisitFields>
nb::class_<T> BindOptions(nb::handle scope, const char* name, const char* doc, Visit visit = {}) {
    nb::class_<T> cls(scope, name, doc);
    OptionsBinder<T> binder(cls);
    visit(binder, binder.Prototype());
    BindFieldsInit(cls, binder.Names());
    BindFieldsRepr(cls, binder.Names());
    return cls;
}

/// `BindOptions`, with a `validate()` method that calls `validate(options)`.
template <typename T, typename Visit = VisitFields>
nb::class_<T> BindOptions(nb::handle scope, const char* name, const char* doc,
                          void (*validate)(const T&), Visit visit = {}) {
    nb::class_<T> cls = BindOptions<T>(scope, name, doc, visit);
    cls.def("validate", validate, kValidateDoc);
    return cls;
}

/// `name` in upper case, as Python enum members are named.
inline std::string UpperName(std::string_view name) {
    std::string upper(name);
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper;
}

/// Binds the enum `E` as `name` with the members of its configuration names (`EnumNames`), each
/// in upper case and documented by its description.
template <typename E> nb::enum_<E> BindEnum(nb::handle scope, const char* name, const char* doc) {
    nb::enum_<E> binding(scope, name, doc);
    for (const auto& entry : config::EnumNames<E>::kValues) {
        const std::string member = UpperName(entry.name);
        const std::string description(entry.description);
        binding.value(member.c_str(), entry.value, description.c_str());
    }
    return binding;
}

}  // namespace terraingen::python
