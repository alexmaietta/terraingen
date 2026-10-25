// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The correction a source chain's seam adds to a later source in an earlier one's gaps.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/source_chain.h"

#include <cstddef>
#include <vector>

namespace terraingen::detail {

/// A correction in meters at the posts of a lattice, read between them by Catmull-Rom
/// interpolation, and 0 at posts never set and off the lattice. Posts are kept in square tiles,
/// only those holding a post that was set.
class SeamField {
  public:
    /// The field that is 0 everywhere.
    SeamField() = default;

    /// A field over `lattice`, 0 at every post until set.
    explicit SeamField(const RasterGrid& lattice);

    /// The correction at world point (`x`, `y`).
    double At(double x, double y) const;

    /// The value at post (`col`, `row`); 0 off the lattice.
    float Post(std::ptrdiff_t col, std::ptrdiff_t row) const;

    /// Sets the value at post (`col`, `row`) of the lattice.
    void Set(std::size_t col, std::size_t row, float value);

    /// The lattice the posts are on.
    const RasterGrid& Lattice() const {
        return lattice_;
    }

  private:
    RasterGrid lattice_{};
    std::size_t tileColumns_ = 0;
    bool set_ = false;
    std::vector<std::vector<float>> tiles_{};
};

/// The correction `seams` add to `later` where `earlier` has no data, solved on the posts of
/// `grid` widened by the seam's reach (`SeamLattice`) with `earlier` and `later` sampled there at
/// the cell size of `grid`. The posts with data near a gap hold `earlier` minus `later`, which the
/// gap's posts are solved to meet; the field is 0 for `ChainSeamMode::None`, when `earlier` has
/// data at every post or at none, and from the seam's reach on.
/// @throws std::invalid_argument as `SeamLattice`.
SeamField BuildSeamField(const Source& earlier, const Source& later, const RasterGrid& grid,
                         const ChainSeams& seams);

}  // namespace terraingen::detail
