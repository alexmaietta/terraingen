/// @file
/// @brief Sources tried in order, each filling the gaps of those before it.
#pragma once

#include "terraingen/sources/source.h"

#include <memory>
#include <vector>

namespace terraingen {

/// Sources tried in order, each filling where those before it have no data, such as a site
/// DEM over a regional one or a DEM over a flat datum. The sources must share coordinates; the
/// chain takes its grid and reference point from the first.
class SourceChain final : public Source {
  public:
    /// The chain of `sources`, first first.
    /// @throws std::invalid_argument for no sources or a null one.
    explicit SourceChain(std::vector<std::shared_ptr<const Source>> sources);

    /// The first source's grid.
    RasterGrid Grid() const override {
        return sources_.front()->Grid();
    }

    /// The first source's reference point.
    Vec2d ReferencePoint() const override {
        return sources_.front()->ReferencePoint();
    }

    /// The first source's projection.
    GeoKeys Keys() const override {
        return sources_.front()->Keys();
    }

    /// Fills from the first source, then each cell still without data from the next source
    /// that has data there, keeping the first source's no-data value.
    void Fill(Raster<float>& heights) const override;

    /// The first height in metres at world point (`x`, `y`) that is not NaN; NaN if no source has
    /// one.
    float HeightAt(double x, double y) const override;

    /// The sources in the order they are tried.
    const std::vector<std::shared_ptr<const Source>>& Sources() const {
        return sources_;
    }

  private:
    std::vector<std::shared_ptr<const Source>> sources_;
};

}  // namespace terraingen
