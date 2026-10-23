/// @file
/// @brief Internal: the topographic shadows of the distal reflectance zones around new craters.
#pragma once

#include "terraingen/albedo/crater_albedo_options.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <vector>

namespace terraingen::detail {

/// How far a jet from a crater's rim runs above the ground, on `azimuths` rays of `samples`
/// samples around the crater, of which `rays` rays from `first` and their first `used` samples
/// are held: those reaching the region evaluated.
struct ShadowProfile {
    std::size_t azimuths = 0;
    std::size_t samples = 0;
    std::size_t first = 0;
    std::size_t rays = 0;
    std::size_t used = 0;
    double start = 0.0;
    double step = 0.0;
    double depth = 1.0;
    std::vector<float> gap;

    /// How deep in shadow the ground is at `distance` meters and `angle` radians from the
    /// crater's center, from 0 in the open to 1 in full shadow.
    double Shade(double distance, double angle) const;

    /// The surface points of the held samples around `center`, row-major by ray.
    std::vector<Vec2d> Points(const Vec2d& center, const Surface& surface) const;

    /// Sets the samples along each held ray from the ground's `heights` there, row-major by ray:
    /// how far a jet from the rim, dropping by at most `drop` meters per sample, runs above them.
    void TraceJets(const std::vector<float>& heights, double drop);
};

/// The rays, of `azimuths` around a center, that reach a region, with one more on each side, and
/// the farthest distance in meters of the region from the center.
struct RayWindow {
    std::size_t first = 0;
    std::size_t rays = 0;
    double far = 0.0;
};

/// The `RayWindow` of `azimuths` rays around `center` over `region`.
RayWindow RaysOver(const Vec2d& center, const Bounds2d& region, const Surface& surface,
                   std::size_t azimuths);

/// The shadow profile of `shadows` from the rim of a crater `radius` meters across to `reach`
/// meters, holding the rays of `window` and the samples out to its far side; heights not yet
/// traced.
ShadowProfile MakeShadowProfile(const ZoneShadows& shadows, double radius, double reach,
                                const RayWindow& window);

}  // namespace terraingen::detail
