// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ReferencedSource`, a source that only gives a reference point, for tests of what
/// layers measure from it.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/raster/grid.h"
#include "terraingen/sources/source.h"

#include <algorithm>

namespace terraingen::testing {

/// A source of flat ground at 0 m without bounds whose reference point is `point`.
class ReferencedSource final : public Source {
  public:
    explicit ReferencedSource(Vec2d point) : point_(point) {}

    RasterGrid Grid() const override {
        return {};
    }
    Vec2d ReferencePoint() const override {
        return point_;
    }

  protected:
    void FillSamples(SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        std::fill(samples.Heights().begin(), samples.Heights().end(), 0.0f);
    }

  private:
    Vec2d point_;
};

}  // namespace terraingen::testing
