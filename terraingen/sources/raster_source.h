// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Heights from a raster held in memory.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/upsampling.h"

#include <memory>
#include <optional>
#include <string>

namespace terraingen {

/// How a `RasterSource` reads its raster.
struct RasterSourceOptions {
    /// The cell size in CRS units the heights resolve, for a DEM that was upsampled or smoothed
    /// before it was saved: `ResolutionAt` gives it in place of the cells' size. Unset uses the
    /// cells'.
    std::optional<double> effectivePixelSize{};
    /// How the cells become heights between them and at sample spacings finer than the cells;
    /// null for cubic interpolation (`DefaultUpsampler`).
    std::shared_ptr<const Upsampler> upsampler{};

    /// Memberwise equality, the upsamplers compared by identity.
    bool operator==(const RasterSourceOptions& other) const {
        return effectivePixelSize == other.effectivePixelSize && upsampler == other.upsampler;
    }
    bool operator!=(const RasterSourceOptions& other) const {
        return !(*this == other);
    }
};

namespace detail {

/// Checks raster source options.
/// @throws std::invalid_argument for an `effectivePixelSize` that is not positive and finite.
void ValidateRasterSourceOptions(const RasterSourceOptions& options);

/// The upsampler of `options`: its own, or cubic interpolation without one.
std::shared_ptr<const Upsampler> OptionUpsampler(const RasterSourceOptions& options);

/// The cells of the whole grid `native` that give the heights of its cells `cells` to the same
/// bits as the whole: those under `cells` with a margin of `reach` cells and one more for
/// rounding, clamped to `native`.
PixelWindow NativeWindow(const RasterGrid& native, int reach, const PixelWindow& cells);

/// Whether a raster source with keys `keys` on `lattice` wraps in longitude: a geographic CRS
/// and an unrotated lattice whose columns span exactly 360 degrees.
bool WrapsLongitude(const GeoKeys& keys, const RasterGrid& lattice);

/// The top-left corner of `lattice`, a raster source's reference point.
inline Vec2d Corner(const RasterGrid& lattice) {
    return {lattice.transform.originX, lattice.transform.originY};
}

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

    /// The cells covered, on the lattice.
    RasterGrid Grid() const {
        return Subgrid(lattice_, cells_);
    }

    /// The source's grid without a window: the lattice.
    const RasterGrid& Lattice() const {
        return lattice_;
    }

    /// Copies cells on the lattice at its spacing or coarser, otherwise upsamples; cells beyond
    /// those covered are no-data. A wrapping source fills each run of columns a turn of
    /// longitude away from the lattice as the lattice's own longitudes.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    void Fill(Raster<float>& heights, double spacing, unsigned threads) const;

    /// The height of the lattice cell centered at (`x`, `y`) at the lattice's spacing or coarser,
    /// otherwise the upsampler's; NaN beyond the cells covered and where there is no data. A
    /// wrapping source reads `x` a whole number of turns into the lattice's longitudes.
    float HeightAt(double x, double y, double spacing) const;

    /// The effective pixel size, or else the native cell size, where `HeightAt` has a height;
    /// infinity elsewhere.
    double ResolutionAt(double x, double y) const;

    /// The upsampler in use, fitted to the cells (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return upsampler_;
    }

  private:
    /// Where a grid on the lattice starts, in lattice cells.
    struct LatticeOffset {
        long long col = 0;
        long long row = 0;
    };

    /// The offset of a grid with transform `target` when it lies on the lattice.
    std::optional<LatticeOffset> OffsetOnLattice(const GeoTransform& target) const;
    /// Copies the cells under `heights`, which starts at `offset`.
    void CopyFromLattice(Raster<float>& heights, LatticeOffset offset) const;
    /// Whether lattice position (`col`, `row`), in cells, is covered.
    bool Covers(double col, double row) const;
    /// The turns of longitude to take from `x` to bring it onto the lattice; 0 unless wrapping.
    double Turns(double x) const;
    /// `Fill` of a grid without wrapping.
    void FillUnwrapped(Raster<float>& heights, double spacing, unsigned threads) const;

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

}  // namespace detail

/// Heights from a raster held in memory, in a projected CRS or the surface's coordinates.
///
/// The heights are used as given, with the raster's no-data value. On the source's own lattice
/// at its cell size or coarser `Fill` copies cells exactly; elsewhere, and at finer spacings,
/// the upsampler gives each cell from the raster's cells, its posts. Cells beyond the raster are
/// no-data. With geographic `keys` and columns spanning 360 degrees the raster wraps in
/// longitude (`detail::WrapsLongitude`), so a file from 0 to 360 also serves -180 to 0.
class RasterSource final : public Source {
  public:
    using Source::Fill;

    /// The source of `heights`.
    /// @param keys The projection outputs are written with.
    /// @throws std::invalid_argument for an empty raster, invalid options, or as
    /// `Upsampler::FitTo`.
    explicit RasterSource(Raster<float> heights, RasterSourceOptions options = {},
                          GeoKeys keys = {});

    /// The source of heights held elsewhere, shared rather than copied.
    /// @throws std::invalid_argument as for a raster, or for a null one.
    explicit RasterSource(std::shared_ptr<const Raster<float>> heights,
                          RasterSourceOptions options = {}, GeoKeys keys = {});

    /// The raster's grid.
    RasterGrid Grid() const override {
        return heights_.Grid();
    }

    /// The top-left corner of the grid.
    Vec2d ReferencePoint() const override {
        return detail::Corner(heights_.Lattice());
    }

    /// The projection given at construction.
    GeoKeys Keys() const override {
        return keys_;
    }

    /// Copies cells on the source's lattice at its cell size or coarser, otherwise upsamples.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    void Fill(Raster<float>& heights, double spacing, unsigned threads) const override {
        heights_.Fill(heights, spacing, threads);
    }

    /// The height of the source cell centered at (`x`, `y`) at the cell size or coarser, otherwise
    /// the upsampler's; NaN beyond the raster and where it has no data.
    float HeightAt(double x, double y, double spacing) const override {
        return heights_.HeightAt(x, y, spacing);
    }

    /// `effectivePixelSize`, or else the raster's cell size in CRS units, the larger side of a
    /// cell; infinity where `HeightAt` is NaN.
    double ResolutionAt(double x, double y) const override {
        return heights_.ResolutionAt(x, y);
    }

    /// How the raster is read.
    const RasterSourceOptions& Options() const {
        return options_;
    }

    /// The upsampler in use, fitted to the raster (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return heights_.UpsamplerInUse();
    }

  private:
    RasterSourceOptions options_;
    GeoKeys keys_;
    detail::RasterHeights heights_;
};

}  // namespace terraingen
