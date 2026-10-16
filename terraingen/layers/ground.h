/// @file
/// @brief The terrain beneath a layer, readable at any point.
#pragma once

#include <functional>

namespace terraingen {

/// Heights of the ground at world positions, NaN where there is none; thread-safe.
using HeightSampler = std::function<float(double x, double y)>;

/// The terrain beneath a layer (its source and the layers before it), readable anywhere.
///
/// Layers that depend on the shape of the ground, such as craters cut to a datum, read it here
/// rather than from cells they changed, so they agree on every grid, tile and point.
/// Implementations must be deterministic and thread-safe.
class Ground {
  public:
    virtual ~Ground() = default;

    /// The ground's height at world (x, y), or NaN where there is no data.
    /// @param spacing Sample spacing in world units.
    virtual float HeightAt(double x, double y, double spacing) const = 0;

    /// A sampler reading `HeightAt` at a fixed spacing; `*this` must outlive it.
    HeightSampler Sampler(double spacing) const {
        return [this, spacing](double x, double y) { return HeightAt(x, y, spacing); };
    }
};

}  // namespace terraingen
