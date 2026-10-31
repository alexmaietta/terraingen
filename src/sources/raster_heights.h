// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `RasterHeights`, the upsampled heights of a window of a post lattice, which raster and
/// GeoTIFF sources serve.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/sources/upsampling.h"

#include <memory>
#include <optional>
#include <string>

namespace terraingen::detail {

/// Checks raster source options.
/// @throws std::invalid_argument for an `effectivePixelSize` that is not positive and finite.
void ValidateRasterSourceOptions(const RasterSourceOptions& options);

/// The cells of the whole grid `native` that give the heights of its cells `cells` to the same
/// bits as the whole: those under `cells` with a margin of `reach` cells and one more for
/// rounding, clamped to `native`.
PixelWindow NativeWindow(const RasterGrid& native, int reach, const PixelWindow& cells);

/// Whether a raster source with keys `keys` on `lattice` wraps in longitude: a geographic CRS
/// and an unrotated lattice whose columns span exactly 360 degrees.
bool WrapsLongitude(const GeoKeys& keys, const RasterGrid& lattice);

/// The heights of a raster source: the posts of a lattice giving its cells `cells`, upsampled
/// between them as the options say.
class RasterHeights {
  public:
    /// @param posts The posts on `lattice`, holding at least `NativeWindow` of `cells`.
    /// @param lattice The whole grid the posts lie on.
    /// @param noData The value of cells copied where the posts have no data, and of cells beyond
    /// those covered; NaN without one.
    /// @param cells A non-empty window of the lattice; the whole lattice gives the heights over
    /// every cell, and also interpolates over the whole of the posts.
    /// @param name What the source is called in errors.
    /// @param wrapLongitude Whether x is a longitude repeating every 360 degrees
    /// (`WrapsLongitude`); the posts then wrap their columns too.
    /// @throws std::invalid_argument as `Upsampler::FitTo`.
    RasterHeights(std::shared_ptr<const PostLattice> posts, const RasterGrid& lattice,
                  std::optional<float> noData, const RasterSourceOptions& options,
                  const PixelWindow& cells, std::string name, bool wrapLongitude = false);

    /// Sets each sample to the height of the lattice cell centered there at the lattice's spacing
    /// or coarser, otherwise the upsampler's (`Upsampler::Fill`); NaN beyond the cells covered and
    /// where there is no data, a grid taking the no-data value for `Source::Fill` to mark. A
    /// wrapping source reads each sample a whole number of turns of longitude into the lattice's
    /// longitudes.
    /// @param threads Threads to fill a grid over; 0 for every hardware thread.
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    void Fill(SampleBlock& samples, double spacing, unsigned threads) const;

    /// The effective pixel size, or else the native cell size, at a covered point whose cell holds
    /// data; infinity elsewhere.
    double ResolutionAt(double x, double y) const;

    /// The native cell size at a covered point whose cell holds data; infinity elsewhere.
    double PostSpacingAt(double x, double y) const;

    /// The upsampler in use, fitted to the cells (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return upsampler_;
    }

  private:
    /// Whether lattice position (`col`, `row`), in cells, is covered.
    bool Covers(double col, double row) const;
    /// Whether (`x`, `y`) is covered and the lattice cell under it holds data.
    bool HasHeight(double x, double y) const;
    /// The cells of `grid` that may be covered: those meeting the covered cells, and one more on
    /// every side.
    PixelWindow CoveredWindow(const RasterGrid& grid) const;
    /// The height `FillUnwrapped` gives cell (`col`, `row`) of `heights`: NaN beyond `window` or
    /// the cells covered, otherwise the cell of `covered`, a fill of `window`, or of `heights`
    /// filled whole when it is null.
    float HeightIn(const Raster<float>& heights, std::size_t col, std::size_t row,
                   const PixelWindow& window, const Raster<float>* covered) const;
    /// `point` moved a whole number of turns of longitude onto the lattice (`Turns`).
    Vec2d Unwrapped(const Vec2d& point) const;
    /// The turns of longitude to take from `x` to bring it onto the lattice; 0 unless wrapping.
    double Turns(double x) const;
    /// `Fill` of a grid, each run of columns a turn of longitude away from the lattice filled
    /// as the lattice's own longitudes.
    void FillGrid(Raster<float>& heights, double spacing, unsigned threads) const;
    /// `Fill` of a grid without wrapping, upsampling only the cells that may be covered when a
    /// fill of them alone gives the same bits (`ExactSubgrid`).
    void FillUnwrapped(Raster<float>& heights, double spacing, unsigned threads) const;
    /// `Fill` of `points`, the points covered upsampled at once.
    void FillPoints(Span<const Vec2d> points, Span<float> heights, double spacing) const;

    std::shared_ptr<const PostLattice> posts_;
    RasterGrid lattice_;
    std::optional<float> noData_;
    PixelWindow cells_;
    std::shared_ptr<const Upsampler> upsampler_;
    double cellSpacing_ = 0.0;
    double resolution_ = 0.0;
    std::string name_;
    bool wrapLongitude_ = false;
};

}  // namespace terraingen::detail
