// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ValueDistribution`, a clamped distribution a parameter is drawn from.
#pragma once

#include <limits>
#include <utility>
#include <vector>

namespace terraingen {

class RandomStream;

/// A distribution a parameter is drawn from, clamped to [`min`, `max`].
struct ValueDistribution {
    /// The family of the distribution, which sets the meaning of `a` and `b`.
    enum class Kind {
        Constant,   ///< Always `a`.
        Uniform,    ///< Uniform on [`a`, `b`].
        Normal,     ///< Mean `a`, standard deviation `b`.
        LogNormal,  ///< Median `a`, standard deviation of the natural logarithm `b`.
        Piecewise,  ///< Uniform within each bin of `edges`, with bin shares from `weights`.
    };
    Kind kind = Kind::Constant;                             ///< The family.
    double a = 0.0;                                         ///< First parameter, per `kind`.
    double b = 0.0;                                         ///< Second parameter, per `kind`.
    double min = -std::numeric_limits<double>::infinity();  ///< Lower clamp.
    double max = std::numeric_limits<double>::infinity();   ///< Upper clamp.
    std::vector<double> edges{};    ///< Piecewise bin edges, strictly increasing.
    std::vector<double> weights{};  ///< Piecewise relative weight of each bin.

    /// Always `value`.
    static ValueDistribution Constant(double value) {
        return {Kind::Constant, value};
    }
    /// Uniform on [`low`, `high`].
    static ValueDistribution Uniform(double low, double high) {
        return {Kind::Uniform, low, high};
    }
    /// Normal of `mean` and standard deviation `sd`; needs a finite `max` (`Clamped`).
    static ValueDistribution Normal(double mean, double sd) {
        return {Kind::Normal, mean, sd};
    }
    /// Log-normal of `median` and standard deviation of the natural logarithm `logSd`; needs a
    /// finite `max` (`Clamped`).
    static ValueDistribution LogNormal(double median, double logSd) {
        return {Kind::LogNormal, median, logSd};
    }
    /// Uniform within each bin of `edges`, with bin shares from `weights`.
    static ValueDistribution Piecewise(std::vector<double> edges, std::vector<double> weights) {
        ValueDistribution distribution{Kind::Piecewise};
        distribution.edges = std::move(edges);
        distribution.weights = std::move(weights);
        return distribution;
    }

    /// This distribution clamped to [`low`, `high`], for chaining:
    /// `ValueDistribution::Normal(10.7, 3.1).Clamped(6.0, 20.0)`.
    ValueDistribution Clamped(double low, double high) const {
        ValueDistribution clamped = *this;
        clamped.min = low;
        clamped.max = high;
        return clamped;
    }

    /// A value drawn from two independent uniforms `u1` and `u2` in [0, 1) (Box-Muller for the
    /// normal kinds, inversion of `u1` for piecewise), clamped to [`min`, `max`].
    double Sample(double u1, double u2) const;

    /// `Sample` of the next two uniforms of `random`, in order.
    double Draw(RandomStream& random) const;

    /// The smallest value `Sample` can return.
    double Lowest() const;

    /// The largest value `Sample` can return.
    double Highest() const;

    /// Memberwise equality.
    bool operator==(const ValueDistribution& other) const {
        return kind == other.kind && a == other.a && b == other.b && min == other.min &&
               max == other.max && edges == other.edges && weights == other.weights;
    }
    bool operator!=(const ValueDistribution& other) const {
        return !(*this == other);
    }
};

/// Checks a value distribution.
/// @throws std::invalid_argument for non-finite or out-of-range parameters, an unbounded normal or
/// log-normal, bad piecewise edges or weights, or edges or weights on another kind.
void ValidateValueDistribution(const ValueDistribution& distribution);

}  // namespace terraingen
