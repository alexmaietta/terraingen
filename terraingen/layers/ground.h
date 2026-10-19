/// @file
/// @brief The terrain beneath a layer, readable at any point.
#pragma once

#include "terraingen/util/geometry.h"

#include <cstddef>
#include <functional>
#include <span>

namespace terraingen {

/// Heights of the ground at world positions, NaN where there is none; thread-safe.
using HeightSampler = std::function<float(double x, double y)>;

/// Heights of the ground at many world positions at once, into a span of the same size;
/// thread-safe.
using HeightsSampler = std::function<void(std::span<const Vec2d> points, std::span<float> heights)>;

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

    /// `HeightAt` at each of `points`, into `heights` of the same size; implementations may read
    /// them all in one pass, to the same values.
    virtual void HeightsAt(std::span<const Vec2d> points, std::span<float> heights,
                           double spacing) const {
        for (std::size_t i = 0; i < points.size(); ++i) {
            heights[i] = HeightAt(points[i].x, points[i].y, spacing);
        }
    }

    /// A sampler reading `HeightAt` at a fixed spacing; `*this` must outlive it.
    HeightSampler Sampler(double spacing) const {
        return [this, spacing](double x, double y) { return HeightAt(x, y, spacing); };
    }

    /// A sampler reading `HeightsAt` at a fixed spacing; `*this` must outlive it.
    HeightsSampler BatchSampler(double spacing) const {
        return [this, spacing](std::span<const Vec2d> points, std::span<float> heights) {
            HeightsAt(points, heights, spacing);
        };
    }
};

}  // namespace terraingen
