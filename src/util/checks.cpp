// SPDX-License-Identifier: BSD-3-Clause

#include "util/checks.h"

#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

[[noreturn]] void Fail(std::string_view what, std::string_view requirement) {
    throw std::invalid_argument(std::string(what) + " must be " + std::string(requirement));
}

}  // namespace

void RequirePositive(double value, std::string_view what) {
    if (!IsPositive(value)) {
        Fail(what, "positive and finite");
    }
}

void RequireNonNegative(double value, std::string_view what) {
    if (!IsNonNegative(value)) {
        Fail(what, "finite and not negative");
    }
}

void RequireFinite(double value, std::string_view what) {
    if (!std::isfinite(value)) {
        Fail(what, "finite");
    }
}

void RequireUnitInterval(double value, std::string_view what) {
    if (!IsInUnitInterval(value)) {
        Fail(what, "in [0, 1]");
    }
}

void RequireDiameter(double diameter) {
    if (!IsPositive(diameter)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
}

}  // namespace terraingen
