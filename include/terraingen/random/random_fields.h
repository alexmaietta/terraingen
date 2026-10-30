// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field list of value distributions and the names of their kinds.
#pragma once

#include "terraingen/random/value_distribution.h"
#include "terraingen/util/option_fields.h"

#include <array>
#include <limits>

namespace terraingen {

template <> struct EnumNames<ValueDistribution::Kind> {
    static constexpr std::array<EnumEntry<ValueDistribution::Kind>, 5> kValues{{
        {"constant", ValueDistribution::Kind::Constant, "Always a."},
        {"uniform", ValueDistribution::Kind::Uniform, "Uniform between a and b."},
        {"normal", ValueDistribution::Kind::Normal, "Normal with mean a and standard deviation b."},
        {"log_normal", ValueDistribution::Kind::LogNormal,
         "Log-normal with median a and standard deviation b of the natural logarithm."},
        {"piecewise", ValueDistribution::Kind::Piecewise,
         "Uniform within each bin between edges, each bin drawn in proportion to its weight."},
    }};
};

template <typename IO> void Fields(IO& io, ValueDistribution& distribution) {
    io("kind", distribution.kind, "Kind of distribution the value is drawn from.");
    io("a", distribution.a,
       "The constant value, the uniform's lower bound, the normal's mean or the log-normal's "
       "median.");
    io("b", distribution.b,
       "The uniform's upper bound, or the standard deviation (of the natural logarithm for a "
       "log-normal); unused for a constant.");
    OptionalBound(io, "min", distribution.min,
                  "Lower bound drawn values are clamped to; unset leaves them unbounded below.", {},
                  -std::numeric_limits<double>::infinity());
    OptionalBound(io, "max", distribution.max,
                  "Upper bound drawn values are clamped to; unset leaves them unbounded above, "
                  "which normal and log-normal kinds do not allow.");
    io("edges", distribution.edges,
       "With piecewise, the edges of the bins, at least two and strictly increasing; values are "
       "uniform within each bin. Empty for every other kind.");
    io("weights", distribution.weights,
       "With piecewise, the relative weight of each bin, one fewer than the edges and not all "
       "zero; each bin draws its share of the total. Empty for every other kind.",
       "", AtLeast(0.0));
}

}  // namespace terraingen
