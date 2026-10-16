/// @file
/// @brief The terrain beneath a layer, readable at any point.
#pragma once

#include <functional>

namespace terraingen {

/// @brief Heights of the ground at world positions, NaN where there is none; thread-safe.
using HeightSampler = std::function<float(double x, double y)>;

/// @brief The terrain beneath a layer (its source and the layers before it), readable anywhere.
///
/// Layers that depend on the shape of the ground, such as craters cut to a datum, read it here
/// rather than from cells the same layer changed, so they agree on every grid, tile and point. The
/// pipeline provides one for a `Terrain`; embedders may provide their own. Implementations must be
/// deterministic and thread-safe.
class Ground {
  public:
    virtual ~Ground() = default;

    /// @brief The ground's height at a point.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param spacing Sample spacing in world units.
    /// @return The height, or NaN where there is no data.
    virtual float HeightAt(double x, double y, double spacing) const = 0;

    /// @brief `HeightAt` at a fixed spacing; `*this` must outlive the result.
    /// @param spacing Sample spacing in world units.
    /// @return A sampler reading this ground.
    HeightSampler Sampler(double spacing) const {
        return [this, spacing](double x, double y) { return HeightAt(x, y, spacing); };
    }
};

}  // namespace terraingen
