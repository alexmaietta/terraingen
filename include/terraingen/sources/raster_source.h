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

class RasterHeights;

/// A source serving the upsampled heights of a window of a lattice: what `RasterSource`
/// and `GeoTiffSource` share.
class RasterHeightsSource : public Source {
  public:
    /// The lattice's cells covered.
    RasterGrid Grid() const final {
        return Subgrid(lattice_, cells_);
    }

    /// The top-left corner of the whole lattice, with or without a window of it.
    Vec2d ReferencePoint() const final {
        return {lattice_.transform.originX, lattice_.transform.originY};
    }

    /// The projection given at construction.
    GeoKeys Keys() const final {
        return keys_;
    }

    /// The effective pixel size, or else the cell size in CRS units, the larger side of a cell;
    /// infinity off the covered cells and where the cell under (`x`, `y`) holds no data.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    double ResolutionAt(double x, double y) const final;

    /// The cell size in CRS units, the larger side of a cell; infinity off the covered cells and
    /// where the cell under (`x`, `y`) holds no data.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    double PostSpacingAt(double x, double y) const final;

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

    /// Copies cells on the lattice at its cell size or coarser, otherwise upsamples.
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    /// @throws GeoTiffError if a file read for the heights cannot be read.
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const final;

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
/// longitude, so a file from 0 to 360 also serves -180 to 0.
class RasterSource final : public detail::RasterHeightsSource {
  public:
    /// The source of `heights`.
    /// @param keys The projection outputs are written with.
    /// @throws std::invalid_argument for an empty raster, invalid options, or as
    /// `Upsampler::FitTo`.
    explicit RasterSource(Raster<float> heights, const RasterSourceOptions& options = {},
                          GeoKeys keys = {});

    /// The upsampler in use, fitted to the raster (`Upsampler::FitTo`).
    const std::shared_ptr<const Upsampler>& UpsamplerInUse() const;

  protected:
    const detail::RasterHeights& Heights() const override;

  private:
    std::shared_ptr<const detail::RasterHeights> heights_;
};

}  // namespace terraingen
