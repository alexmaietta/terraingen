#pragma once

#include "terraingen/sources/source.h"

#include <memory>
#include <vector>

namespace terraingen {

/// Sources tried in order, each filling where those before it have no data: a high-resolution DEM
/// of a site over a coarser regional one, or a DEM with a flat datum under its gaps. The sources
/// must share coordinates. The chain covers the first source's grid and measures noise from its
/// reference point.
class SourceChain final : public Source {
  public:
    /// @throws std::invalid_argument for no sources or a null one.
    explicit SourceChain(std::vector<std::shared_ptr<const Source>> sources);

    RasterGrid Grid() const override {
        return sources_.front()->Grid();
    }

    Vec2d ReferencePoint() const override {
        return sources_.front()->ReferencePoint();
    }

    /// Fills from the first source, then each cell still without data from the next source that has
    /// data there, keeping the first source's no-data value.
    void Fill(Raster<float>& heights) const override;

    /// The first source's height at (`x`, `y`) that is not NaN, or NaN if none has one.
    float HeightAt(double x, double y) const override;

    const std::vector<std::shared_ptr<const Source>>& Sources() const {
        return sources_;
    }

  private:
    std::vector<std::shared_ptr<const Source>> sources_;
};

}  // namespace terraingen
