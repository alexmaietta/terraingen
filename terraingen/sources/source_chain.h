/// @file
/// @brief Sources tried in order, each filling the gaps of those before it.
#pragma once

#include "terraingen/sources/source.h"

#include <memory>
#include <vector>

namespace terraingen {

/// @brief Sources tried in order, each filling where those before it have no data, such as a site
/// DEM over a regional one or a DEM over a flat datum. The sources must share coordinates; the
/// chain takes its grid and reference point from the first.
class SourceChain final : public Source {
  public:
    /// @brief The chain of `sources`.
    /// @param sources At least one source, none null, first first.
    /// @throws std::invalid_argument for no sources or a null one.
    explicit SourceChain(std::vector<std::shared_ptr<const Source>> sources);

    /// @brief The first source's grid.
    /// @return The grid.
    RasterGrid Grid() const override {
        return sources_.front()->Grid();
    }

    /// @brief The first source's reference point.
    /// @return The reference point in world units.
    Vec2d ReferencePoint() const override {
        return sources_.front()->ReferencePoint();
    }

    /// @brief Fills from the first source, then each cell still without data from the next source
    /// that has data there, keeping the first source's no-data value.
    /// @param heights Raster to fill.
    void Fill(Raster<float>& heights) const override;

    /// @brief The first height at a point that is not NaN.
    /// @param x World x.
    /// @param y World y.
    /// @return Height in metres, or NaN if no source has one.
    float HeightAt(double x, double y) const override;

    /// @brief The sources.
    /// @return The sources in the order they are tried.
    const std::vector<std::shared_ptr<const Source>>& Sources() const {
        return sources_;
    }

  private:
    std::vector<std::shared_ptr<const Source>> sources_;
};

}  // namespace terraingen
