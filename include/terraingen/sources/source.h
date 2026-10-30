// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The source interface: where a terrain's heights come from.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/util/span.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// Where a terrain's heights come from: a DEM, synthetic noise, or a source of your own. A source
/// only loads or generates heights; how its posts become heights at finer sample spacings is its
/// upsampler's job (`Upsampler`). Layers add relief to what the source gives, then filters
/// transform the result.
///
/// Heights are a function of position and sample spacing only, so points, tiles and grids at the
/// same spacing agree: every query goes through `FillSamples`, the one method a source
/// evaluates. Sources must be deterministic and thread safe: a terrain may be evaluated tile by
/// tile, in parallel.
class Source {
  public:
    virtual ~Source() = default;

    /// The grid the source covers, which a terrain is evaluated over unless another is given. A
    /// source without bounds, such as synthetic noise, gives a grid without cells whose
    /// transform places its posts, if it has any (`BoundedGrid`).
    virtual RasterGrid Grid() const = 0;

    /// A point near the source's data, in world units, that noise layers measure positions from,
    /// keeping single-precision noise accurate where coordinates are large; the origin by default.
    virtual Vec2d ReferencePoint() const {
        return {};
    }

    /// The projection outputs over the source's grid are written with; none by default.
    virtual GeoKeys Keys() const {
        return {};
    }

    /// Overwrites every sample of `samples` with the source's height there (`FillSamples`); a
    /// grid's cells without data then hold its no-data value, NaN if it has none and any is
    /// missing.
    /// @param spacing Sample spacing in the source's units (meters on a plane, degrees on a
    /// body): finer than the source's posts, they are refined to it.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    void Fill(SampleBlock& samples, double spacing, unsigned threads) const;

    /// `Fill` of every cell of `heights`, placed by its transform.
    /// @param spacing Sample spacing in the source's units.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    void Fill(Raster<float>& heights, double spacing, unsigned threads) const;

    /// `Fill` on every hardware thread.
    void Fill(Raster<float>& heights, double spacing) const {
        Fill(heights, spacing, 0);
    }

    /// `Fill` at the grid's own spacing, its cell size (`RasterGrid::PixelSize`), on every
    /// hardware thread.
    void Fill(Raster<float>& heights) const {
        Fill(heights, RasterGrid::FromRaster(heights).PixelSize(), 0);
    }

    /// `Fill` of the cells of `grid` straight into `heights`, row-major from row 0, NaN where the
    /// source has no data.
    /// @param spacing Sample spacing in the source's units.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    /// @throws std::invalid_argument for `heights` not of `grid`'s size.
    void Fill(const RasterGrid& grid, Span<float> heights, double spacing, unsigned threads) const;

    /// The height in meters at world point (`x`, `y`) at a sample spacing of `spacing` in the
    /// source's units, or NaN where the source has no data: what `Fill` gives a cell centered
    /// there at that spacing, to the same bits.
    float HeightAt(double x, double y, double spacing) const;

    /// Sets each of `heights` to `HeightAt` the point of `points` at its index, to the same bits,
    /// in one `FillSamples` on one thread.
    /// @throws std::invalid_argument for spans of different sizes.
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const;

    /// The cell size in CRS units (degrees in longitude and latitude) of the measured data giving
    /// the height at world point (`x`, `y`), or its effective pixel size, below which the source
    /// resolves no features; infinity where no measured data gives it, as for synthetic heights,
    /// the default, and where the source has no height.
    virtual double ResolutionAt(double /*x*/, double /*y*/) const {
        return std::numeric_limits<double>::infinity();
    }

    /// The spacing in CRS units of the posts giving the height at world point (`x`, `y`) before
    /// any upsampling, measured or generated; infinity where no posts give it, as where heights
    /// are evaluated at every point, the default, and where the source has no height.
    virtual double PostSpacingAt(double /*x*/, double /*y*/) const {
        return std::numeric_limits<double>::infinity();
    }

    /// A copy of the source whose seeds draw from `seed` (`ReseedWord`), sharing any data it
    /// read; null when the copy would equal the source, as for measured data (the default), so
    /// `Terrain::Reseed` keeps it.
    virtual std::shared_ptr<const Source> Reseeded(std::uint64_t /*seed*/) const {
        return nullptr;
    }

  protected:
    /// Sets every sample of `samples` to the source's height there at a sample spacing of
    /// `spacing`, the same bits for a grid's cell as for a point at its center. A sample without
    /// data is set to NaN, or a grid's cell to the no-data value the source gives its raster.
    /// @param threads Threads to split a grid over; 0 for every hardware thread.
    virtual void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const = 0;
};

/// The grid `source` covers, or none for a source without bounds (a grid without cells).
inline std::optional<RasterGrid> BoundedGrid(const Source& source) {
    RasterGrid grid = source.Grid();
    if (grid.width == 0 || grid.height == 0) {
        return std::nullopt;
    }
    return grid;
}

}  // namespace terraingen
