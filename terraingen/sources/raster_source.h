/// @file
/// @brief Heights from a raster held in memory.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <optional>
#include <string>

namespace terraingen {

/// How a `RasterSource` reads its raster.
struct RasterSourceOptions {
    /// Cell size to resample to, in CRS units; unset keeps the raster's cells.
    std::optional<double> pixelSize{};
    /// Interpolation for resampling and for cells off the source's lattice.
    ResampleMethod resampling = ResampleMethod::Cubic;
    /// Standard deviation in CRS units of a Gaussian low-pass applied to the heights once, before
    /// any resampling: the low frequencies of a DEM to add finer synthetic detail to. 0 keeps
    /// every frequency.
    double lowPass = 0.0;

    /// Memberwise equality.
    bool operator==(const RasterSourceOptions& other) const {
        return pixelSize == other.pixelSize && resampling == other.resampling &&
               lowPass == other.lowPass;
    }
    bool operator!=(const RasterSourceOptions& other) const {
        return !(*this == other);
    }
};

namespace detail {

/// Checks raster source options.
/// @throws std::invalid_argument for a `pixelSize` that is not positive and finite, or a negative
/// or non-finite `lowPass`.
void ValidateRasterSourceOptions(const RasterSourceOptions& options);

/// The grid a raster source over the whole grid `native` covers: `native`, resampled to
/// `pixelSize` when set.
/// @throws std::invalid_argument for a rotated `native` that would need resampling.
RasterGrid SourceLattice(const RasterGrid& native, const RasterSourceOptions& options);

/// The cells of the whole grid `native` that give the heights of `cells` of its
/// `SourceLattice` to the same bits as the whole: those under `cells` with margins for the
/// low-pass's kernel and the interpolation, clamped to `native`.
PixelWindow NativeWindow(const RasterGrid& native, const RasterSourceOptions& options,
                         const PixelWindow& cells);

/// The heights of a raster source: a window of a raster, low-passed and resampled as the options
/// say, giving the cells `cells` of its lattice (`SourceLattice`).
class RasterHeights {
  public:
    /// @param native A window of the raster at `placement`, covering `NativeWindow` of `cells`.
    /// @param cells A non-empty window of the lattice; the whole lattice gives the heights over
    /// every cell, and also interpolates over the whole of the raster.
    /// @param name What the source is called in errors.
    RasterHeights(std::shared_ptr<const Raster<float>> native, const RasterPlacement& placement,
                  const RasterSourceOptions& options, const PixelWindow& cells, std::string name);

    /// The cells covered, on the lattice.
    RasterGrid Grid() const {
        return Subgrid(lattice_, cells_);
    }

    /// The source's grid without a window: the lattice.
    const RasterGrid& Lattice() const {
        return lattice_;
    }

    /// Copies cells on the lattice, otherwise samples at cell centres; cells beyond those covered
    /// are no-data.
    /// @throws std::invalid_argument for a rotated grid off the lattice.
    void Fill(Raster<float>& heights) const;

    /// The height of the lattice cell centred at (`x`, `y`) when there is one, otherwise the raster
    /// interpolated there; NaN beyond the cells covered and where there is no data.
    float HeightAt(double x, double y) const;

    /// The native cell size, coarsened to twice the low-pass's deviation, where `HeightAt` has a
    /// height; infinity elsewhere.
    double ResolutionAt(double x, double y) const;

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
    /// Whether lattice cell (`col`, `row`) is covered.
    bool Covers(double col, double row) const;

    std::shared_ptr<const Raster<float>> native_;
    RasterPlacement nativePlacement_;
    std::shared_ptr<const Raster<float>> heights_;
    RasterPlacement heightsPlacement_;
    RasterGrid lattice_;
    PixelWindow cells_;
    bool clipped_ = false;
    ResampleMethod resampling_ = ResampleMethod::Cubic;
    double resolution_ = 0.0;
    std::string name_;
};

}  // namespace detail

/// Heights from a raster held in memory, in a projected CRS or the surface's coordinates,
/// optionally low-passed and resampled to square cells of `pixelSize` over its extent.
///
/// The heights are used as given, with the raster's no-data value. On the source's own lattice
/// `Fill` copies cells exactly; on any other grid each cell is the raster interpolated at its
/// centre with `resampling`. Cells beyond the raster are no-data.
class RasterSource final : public Source {
  public:
    /// The source of `heights`.
    /// @param keys The projection outputs are written with.
    /// @throws std::invalid_argument for an empty raster, a `pixelSize` that is not positive and
    /// finite, a negative or non-finite `lowPass`, or a rotated raster that would need
    /// resampling.
    explicit RasterSource(Raster<float> heights, RasterSourceOptions options = {},
                          GeoKeys keys = {});

    /// The source of heights held elsewhere, shared rather than copied.
    /// @throws std::invalid_argument as for a raster, or for a null one.
    explicit RasterSource(std::shared_ptr<const Raster<float>> heights,
                          RasterSourceOptions options = {}, GeoKeys keys = {});

    /// The raster's grid, resampled to `pixelSize` when set.
    RasterGrid Grid() const override {
        return heights_.Grid();
    }

    /// The top-left corner of the grid.
    Vec2d ReferencePoint() const override;

    /// The projection given at construction.
    GeoKeys Keys() const override {
        return keys_;
    }

    /// Copies cells on the source's lattice, otherwise samples the raster at cell centres.
    /// @throws std::invalid_argument for a rotated grid off the source's lattice.
    void Fill(Raster<float>& heights) const override {
        heights_.Fill(heights);
    }

    /// The height of the source cell centred at (`x`, `y`) when there is one, otherwise of the
    /// raster interpolated there with `resampling` (`SampleRaster`); NaN beyond the raster and
    /// where it has no data.
    float HeightAt(double x, double y) const override {
        return heights_.HeightAt(x, y);
    }

    /// The raster's cell size in CRS units, the larger side of a cell, or twice `lowPass` when
    /// that is larger; infinity where `HeightAt` is NaN.
    double ResolutionAt(double x, double y) const override {
        return heights_.ResolutionAt(x, y);
    }

    /// How the raster is read.
    const RasterSourceOptions& Options() const {
        return options_;
    }

  private:
    RasterSourceOptions options_;
    GeoKeys keys_;
    detail::RasterHeights heights_;
};

}  // namespace terraingen
