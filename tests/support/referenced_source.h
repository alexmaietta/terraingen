// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ReferencedBase`, a terrain base whose source only gives a reference point, for tests
/// of what layers measure from it.
#pragma once

#include "terraingen/sources/terrain_base.h"

#include <algorithm>
#include <memory>

namespace terraingen::testing {

/// A source of flat ground at 0 m without bounds whose reference point is `point`.
class ReferencedSource final : public Source {
  public:
    using Source::Fill;

    explicit ReferencedSource(Vec2d point) : point_(point) {}

    RasterGrid Grid() const override {
        return {};
    }
    Vec2d ReferencePoint() const override {
        return point_;
    }
    void Fill(Raster<float>& heights, double /*spacing*/) const override {
        std::fill(heights.Values().begin(), heights.Values().end(), 0.0f);
    }
    float HeightAt(double /*x*/, double /*y*/, double /*spacing*/) const override {
        return 0.0f;
    }

  private:
    Vec2d point_;
};

/// A base on `surface` whose source's reference point is `point`.
inline TerrainBase ReferencedBase(Vec2d point, Surface surface = {}) {
    return {surface, std::make_shared<ReferencedSource>(point)};
}

}  // namespace terraingen::testing
