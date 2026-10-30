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
#include <utility>

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

    /// Sets each sample to the height of the lattice cell centered there at the lattice's spacing
    /// or coarser, otherwise the upsampler's (`Upsampler::Fill`); NaN, or a grid's no-data value,
    /// beyond the cells covered and where there is no data. A wrapping source reads each sample
    /// a whole number of turns of longitude into the lattice's longitudes.
    /// @param threads Threads to fill a grid over; 0 for every hardware thread.
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    void Fill(SampleBlock& samples, double spacing, unsigned threads) const;

    /// The effective pixel size, or else the native cell size, where `Fill` gives a height;
    /// infinity elsewhere.
    double ResolutionAt(double x, double y) const;

    /// The native cell size where `Fill` gives a height; infinity elsewhere.
    double PostSpacingAt(double x, double y) const;

    /// The upsampler in use, fitted to the cells (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return upsampler_;
    }

  private:
    /// Whether lattice position (`col`, `row`), in cells, is covered.
    bool Covers(double col, double row) const;
    /// Whether `Fill` gives (`x`, `y`) a height at the lattice's spacing.
    bool HasHeight(double x, double y) const;
    /// The turns of longitude to take from `x` to bring it onto the lattice; 0 unless wrapping.
    double Turns(double x) const;
    /// `Fill` of a grid, each run of columns a turn of longitude away from the lattice filled
    /// as the lattice's own longitudes.
    void FillGrid(Raster<float>& heights, double spacing, unsigned threads) const;
    /// `Fill` of a grid without wrapping.
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

/// A source serving the heights of a window of a lattice (`RasterHeights`): what `RasterSource`
/// and `GeoTiffSource` share.
class RasterHeightsSource : public Source {
  public:
    /// The lattice's cells covered.
    RasterGrid Grid() const final {
        return Subgrid(lattice_, cells_);
    }

    /// The top-left corner of the whole lattice, with or without a window of it.
    Vec2d ReferencePoint() const final {
        return Corner(lattice_);
    }

    /// The projection given at construction.
    GeoKeys Keys() const final {
        return keys_;
    }

    /// The effective pixel size, or else the cell size in CRS units, the larger side of a cell;
    /// infinity where the source has no height.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    double ResolutionAt(double x, double y) const final {
        return Heights().ResolutionAt(x, y);
    }

    /// The cell size in CRS units, the larger side of a cell; infinity where the source has no
    /// height.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    double PostSpacingAt(double x, double y) const final {
        return Heights().PostSpacingAt(x, y);
    }

  protected:
    /// @param lattice The whole grid the heights lie on.
    /// @param cells The window of `lattice` covered.
    /// @param keys The projection outputs are written with.
    RasterHeightsSource(const RasterGrid& lattice, const PixelWindow& cells, GeoKeys keys)
        : lattice_(lattice), cells_(cells), keys_(std::move(keys)) {}

    /// The whole grid the heights lie on.
    const RasterGrid& Lattice() const {
        return lattice_;
    }

    /// The window of the lattice covered.
    const PixelWindow& Cells() const {
        return cells_;
    }

    /// Copies cells on the lattice at its cell size or coarser, otherwise upsamples
    /// (`RasterHeights::Fill`).
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const final {
        Heights().Fill(samples, spacing, threads);
    }

    /// The heights served.
    virtual const RasterHeights& Heights() const = 0;

  private:
    RasterGrid lattice_;
    PixelWindow cells_;
    GeoKeys keys_;
};

}  // namespace detail

/// Heights from a raster held in memory, in a projected CRS or the surface's coordinates.
///
/// The heights are used as given, with the raster's no-data value. On the source's own lattice
/// at its cell size or coarser `Fill` copies cells exactly; elsewhere, and at finer spacings,
/// the upsampler gives each cell from the raster's cells, its posts. Cells beyond the raster are
/// no-data. With geographic `keys` and columns spanning 360 degrees the raster wraps in
/// longitude (`detail::WrapsLongitude`), so a file from 0 to 360 also serves -180 to 0.
class RasterSource final : public detail::RasterHeightsSource {
  public:
    /// The source of `heights`.
    /// @param keys The projection outputs are written with.
    /// @throws std::invalid_argument for an empty raster, invalid options, or as
    /// `Upsampler::FitTo`.
    explicit RasterSource(Raster<float> heights, const RasterSourceOptions& options = {},
                          GeoKeys keys = {});

    /// The upsampler in use, fitted to the raster (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const {
        return heights_.UpsamplerInUse();
    }

  protected:
    const detail::RasterHeights& Heights() const override {
        return heights_;
    }

  private:
    detail::RasterHeights heights_;
};

}  // namespace terraingen
