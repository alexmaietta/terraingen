/// @file
/// @brief `ValueDistribution`, a clamped distribution a parameter is drawn from.
#pragma once

#include <limits>

namespace terraingen {

/// @brief A distribution a parameter is drawn from, clamped to [`min`, `max`].
struct ValueDistribution {
    /// @brief The family of the distribution, which sets the meaning of `a` and `b`.
    enum class Kind {
        Constant,   ///< Always `a`.
        Uniform,    ///< Uniform on [`a`, `b`].
        Normal,     ///< Mean `a`, standard deviation `b`.
        LogNormal,  ///< Median `a`, standard deviation of the natural logarithm `b`.
    };
    Kind kind = Kind::Constant;                             ///< The family.
    double a = 0.0;                                         ///< First parameter, per `kind`.
    double b = 0.0;                                         ///< Second parameter, per `kind`.
    double min = -std::numeric_limits<double>::infinity();  ///< Lower clamp.
    double max = std::numeric_limits<double>::infinity();   ///< Upper clamp.

    /// @brief A value drawn from two independent uniforms (Box-Muller for the normal kinds).
    /// @param u1 A uniform in [0, 1).
    /// @param u2 Another uniform in [0, 1).
    /// @return The value, clamped to [`min`, `max`].
    double Sample(double u1, double u2) const;

    /// @brief The smallest value `Sample` can return.
    /// @return The lower bound.
    double Lowest() const;

    /// @brief The largest value `Sample` can return.
    /// @return The upper bound.
    double Highest() const;

    /// @brief Memberwise equality.
    /// @return True when every member is equal.
    bool operator==(const ValueDistribution&) const = default;
};

/// @brief A standard normal from two independent uniforms, by the Box-Muller transform.
/// @param u1 A uniform in [0, 1).
/// @param u2 Another uniform in [0, 1).
/// @return sqrt(-2 ln(1 - `u1`)) cos(2 pi `u2`).
double StandardNormal(double u1, double u2);

/// @brief Checks a value distribution.
/// @param distribution The distribution to check.
/// @throws std::invalid_argument for parameters that are not finite, a uniform with `b` < `a`, a
/// negative standard deviation, a log-normal with a non-positive median, `min` above `max`, or a
/// normal or log-normal without a finite `max`.
void ValidateValueDistribution(const ValueDistribution& distribution);

}  // namespace terraingen
