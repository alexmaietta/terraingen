/// @file
/// @brief The terrain beneath a layer, readable at any point.
#pragma once

#include "terraingen/util/geometry.h"
#include "terraingen/util/span.h"
#include "terraingen/util/surface.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

namespace terraingen {

/// Heights of the ground at world positions, NaN where there is none; thread-safe.
using HeightSampler = std::function<float(double x, double y)>;

/// Heights of the ground at many world positions at once, into a span of the same size;
/// thread-safe.
using HeightsSampler = std::function<void(Span<const Vec2d> points, Span<float> heights)>;

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
    virtual void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
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
        return [this, spacing](Span<const Vec2d> points, Span<float> heights) {
            HeightsAt(points, heights, spacing);
        };
    }
};

/// The four points `step` meters east, west, north and south of `center` on `surface`, in that
/// order, which `GradientFromHeights` reads a gradient from.
std::array<Vec2d, 4> GradientPoints(const Surface& surface, const Vec2d& center, double step);

/// The gradient (east, north, rise over run) by central differences of `heights` read at
/// `GradientPoints` `step` meters out; nothing where any difference is not finite.
std::optional<Vec2d> GradientFromHeights(Span<const float> heights, double step);

/// The gradient (east, north, rise over run) of `ground` at `center` by central differences
/// `step` meters out (`GradientPoints`); nothing where the ground has no data there.
/// @param spacing Sample spacing in meters the ground is read at.
std::optional<Vec2d> GroundGradient(const Ground& ground, const Surface& surface,
                                    const Vec2d& center, double step, double spacing);

/// The heights of `ground` at `offsets` (meters east and north in the local frame of `center`),
/// into `heights` of the same size, with `fallback` where the ground has no data.
/// @param spacing Sample spacing in meters the ground is read at.
void GroundAround(const Ground& ground, const Surface& surface, const Vec2d& center,
                  Span<const Vec2d> offsets, double spacing, double fallback, Span<double> heights);

}  // namespace terraingen
