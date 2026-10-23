/// @file
/// @brief `ValueDistribution`, a clamped distribution a parameter is drawn from.
#pragma once

#include <limits>
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
/// @throws std::invalid_argument for parameters that are not finite, a uniform with `b` < `a`, a
/// negative standard deviation, a log-normal with a non-positive median, `min` above `max`, a
/// normal or log-normal without a finite `max`, a piecewise kind without at least two strictly
/// increasing finite edges and one non-negative weight per bin summing above zero, or edges or
/// weights on any other kind.
void ValidateValueDistribution(const ValueDistribution& distribution);

}  // namespace terraingen
