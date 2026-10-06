#pragma once

#include "terraingen/layers/craters/apply.h"

namespace terraingen {

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

    /// Height at (`x`, `y`) sampled every `spacing` world units, or NaN where there is no data.
    virtual float HeightAt(double x, double y, double spacing) const = 0;

    /// `HeightAt` at `spacing` as a `HeightSampler`; `*this` must outlive it.
    HeightSampler Sampler(double spacing) const {
        return [this, spacing](double x, double y) { return HeightAt(x, y, spacing); };
    }
};

}  // namespace terraingen
