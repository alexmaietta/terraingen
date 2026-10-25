// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Sources tried in order, each filling the gaps of those before it.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <mutex>
#include <vector>

namespace terraingen {

namespace detail {
class SeamField;
}  // namespace detail

/// How a `SourceChain` joins a source to the gaps of the sources before it.
enum class ChainSeamMode {
    None,     ///< Each cell takes the first source with data, unchanged.
    Feather,  ///< Fades the edge's height difference out across a band of the gap.
    Height,   ///< Shifts the later source by the edge's height difference, spread smoothly (C0).
    Slope,    ///< Shifts the later source so heights and slopes both match at the edge (C1).
};

/// How a `SourceChain` joins each source to the gaps of those before it.
///
/// Along the edge of each gap the earlier source's height minus the later one's is measured at
/// the earlier source's posts. `Feather` adds that difference, carried from the nearest edge post,
/// to the later source and fades it to 0 over `width`. `Height` spreads it into the gap as the
/// solution of (Laplacian - 1/L^2) d = 0 with L the `decayLength`, so the later source keeps its
/// relief and only its level shifts; `Slope` solves (Laplacian - 1/L^2)^2 d = 0, matching slopes as
/// well. In a hole the shift spans the hole; beyond a source's edge it fades out over L and is 0
/// from `kSeamReachDecays` L on.
struct ChainSeams {
    ChainSeamMode mode = ChainSeamMode::None;
    /// Width of `Feather`'s band, in the sources' units (meters on a plane, degrees on a body);
    /// positive and finite.
    double width = 50.0;
    /// Decay length L of `Height` and `Slope`, in the sources' units; positive and finite.
    double decayLength = 50.0;

    /// Memberwise equality.
    bool operator==(const ChainSeams& other) const {
        return mode == other.mode && width == other.width && decayLength == other.decayLength;
    }
    bool operator!=(const ChainSeams& other) const {
        return !(*this == other);
    }
};

/// Decay lengths from a gap's edge beyond which `Height` and `Slope` add nothing.
inline constexpr double kSeamReachDecays = 3.0;

/// Checks seam options.
/// @throws std::invalid_argument for a `width` or `decayLength` that is not positive and finite.
void ValidateChainSeams(const ChainSeams& seams);

/// The distance from a gap's edge in the sources' units over which `seams` change the later
/// source: `width` for `Feather`, `kSeamReachDecays` times `decayLength` for `Height` and `Slope`,
/// 0 for `None`.
double SeamReach(const ChainSeams& seams);

/// The lattice a seam at the edge of a source covering `grid` is solved on: `grid`'s posts,
/// widened on every side by enough posts to cover `SeamReach` and 3 more.
/// @throws std::invalid_argument as `ValidateChainSeams`, for a grid whose cells are not of a
/// positive and finite size, or for a lattice more than 2^31 - 1 posts a side.
RasterGrid SeamLattice(const RasterGrid& grid, const ChainSeams& seams);

/// Sources tried in order, each filling where those before it have no data, such as a site
/// DEM over a regional one or a DEM over a flat datum. The sources must share coordinates; the
/// chain takes its grid and reference point from the first.
///
/// With seams (`ChainSeams`) each source is matched to the chain of those before it where it
/// fills their gaps. The match is solved once, on first use, on the posts of the source before it
/// (`SeamLattice`, at the cell size of its grid), and read between the posts by Catmull-Rom
/// interpolation, so points, tiles and grids still agree. A source without bounds gives no lattice
/// and the source after it is not matched. On a body the lattice's degrees of longitude count as
/// degrees of latitude. No-data inside the later source stays no-data.
class SourceChain final : public Source {
  public:
    using Source::Fill;

    /// The chain of `sources`, first first, joined as `seams` says.
    /// @throws std::invalid_argument for no sources or a null one, as `ValidateChainSeams`, or as
    /// `SeamLattice` for a seam at the edge of a source with bounds.
    explicit SourceChain(std::vector<std::shared_ptr<const Source>> sources, ChainSeams seams = {});

    ~SourceChain() override;

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
    /// that has data there, matched by the seams, keeping the first source's no-data value; each
    /// source upsamples its own posts.
    void Fill(Raster<float>& heights, double spacing, unsigned threads) const override;

    /// The first height in meters at world point (`x`, `y`) that is not NaN, matched by the seams;
    /// NaN if no source has one.
    float HeightAt(double x, double y, double spacing) const override;

    /// The resolution of the first source with a height at (`x`, `y`) at its coarsest, an
    /// infinite spacing; infinity if no source has one.
    double ResolutionAt(double x, double y) const override;

    /// The sources in the order they are tried.
    const std::vector<std::shared_ptr<const Source>>& Sources() const {
        return sources_;
    }

    /// How each source is joined to the gaps of those before it.
    const ChainSeams& Seams() const {
        return seams_;
    }

  private:
    /// The correction added to the last source, solved on first use.
    const detail::SeamField& Seam() const;

    std::vector<std::shared_ptr<const Source>> sources_;
    ChainSeams seams_;
    /// The chain of every source but the last, which the last is matched to; null without seams
    /// or with one source.
    std::shared_ptr<const Source> prefix_;
    mutable std::once_flag seamed_;
    mutable std::shared_ptr<const detail::SeamField> seam_;
};

}  // namespace terraingen
