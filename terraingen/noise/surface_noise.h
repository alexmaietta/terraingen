/// @file
/// @brief Noise layer stacks evaluated at points of a plane or a body's surface.
#pragma once

#include "terraingen/noise/noise_layers.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <vector>

namespace terraingen {

/// The summed noise of `layers` at surface (x, y): 2D relative to `origin` on a plane (keeping
/// single precision accurate), 3D at the point's 3D position on a body; 0 for no layers.
/// @param origin Where plane coordinates are measured from; unused on a body.
float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y);

}  // namespace terraingen
