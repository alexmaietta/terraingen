/// @file
/// @brief The terrain beneath a layer, readable at any point.
#pragma once

#include <functional>

namespace terraingen {

/// Heights of the ground beneath a layer at world positions, NaN where there is none; must be safe
/// to call from several threads at once.
using HeightSampler = std::function<float(double x, double y)>;

/// The terrain beneath a layer: its source and the layers before it, readable anywhere, beyond the
/// grid the layer is applied to and around a point it is queried at. Layers that depend on the
/// shape of the ground, such as craters cut to a datum, read it here rather than from the cells
/// earlier parts of the same layer changed, so they give the same result on every grid, tile and
/// point.
///
/// The pipeline provides it for a `Terrain`; an embedding program can provide its own, such as one
/// reading its DEMs. Must be deterministic and safe to call from several threads at once.
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
