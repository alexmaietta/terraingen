// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The albedo source and layer interfaces, albedo compositions and their evaluation.
#pragma once

#include "terraingen/layers/ground.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace terraingen {

/// The most bands an albedo has: three for red, green and blue.
inline constexpr std::size_t kMaxAlbedoBands = 3;

/// An albedo at one point: the first `Bands()` values are used, the rest are 0.
using AlbedoSample = std::array<float, kMaxAlbedoBands>;

/// How an albedo is evaluated.
struct AlbedoContext {
    /// Sample spacing in meters, positive and finite: detail that cannot be resolved at it is left
    /// out.
    double spacing = 1.0;
    /// Threads a grid may be split over; 0 for every hardware thread. The result does not depend
    /// on it.
    unsigned threads = 1;
    /// The terrain's heights, for layers shaped by the ground; null leaves it flat. It must outlive
    /// the evaluation.
    const Ground* ground = nullptr;
};

/// Checks an evaluation context.
/// @throws std::invalid_argument if `context.spacing` is not positive and finite.
void ValidateAlbedoContext(const AlbedoContext& context);

/// Where an albedo starts: a map from orbit, a synthetic color, or a source of your own.
/// Values are reflectances (normal albedo, or reflectance at a wavelength), one per band.
///
/// Sources must be deterministic and thread safe, and `Fill` must give every cell the bits
/// `ValueAt` gives at its center, so tiles agree with the whole.
class AlbedoSource {
  public:
    virtual ~AlbedoSource() = default;

    /// The number of bands: 1 for a single band, or 3 for red, green and blue.
    virtual std::size_t Bands() const = 0;

    /// The albedo at world point (`x`, `y`); NaN in every band where the source has no data.
    virtual AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const = 0;

    /// Sets every cell of each band to `ValueAt` at its center; the default does exactly
    /// that, cell by cell, on `context.threads` threads.
    /// @param bands `Bands()` rasters on one grid.
    virtual void Fill(std::vector<Raster<float>>& bands, const AlbedoContext& context) const;
};

/// A change to an albedo: bright ejecta around fresh craters, or a layer of your own.
///
/// Layers must be deterministic and thread safe, and `Apply` must give every cell the bits
/// `ApplyAt` gives at its center.
class AlbedoLayer {
  public:
    virtual ~AlbedoLayer() = default;

    /// Applies the layer in place to `value`, the albedo at world point (`x`, `y`); NaN stays NaN.
    /// @param bands Bands of `value` in use.
    virtual void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                         const AlbedoContext& context) const = 0;

    /// Applies the layer in place to every cell of `bands` (one raster per band on one grid); the
    /// default calls `ApplyAt` at each cell center on `context.threads` threads.
    virtual void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const;
};

/// An albedo as a composition: values from `source`, then each of `layers` in order.
struct Albedo {
    std::shared_ptr<const AlbedoSource> source;              ///< Where values start; required.
    std::vector<std::shared_ptr<const AlbedoLayer>> layers;  ///< Applied in order; none null.
    /// Sets how a grid's cell size turns into a spacing in meters.
    Surface surface{};
};

/// Evaluates `albedo` over `grid`: one raster per band, with no-data NaN.
/// @param context `spacing` in meters, raised to the grid's spacing on the albedo's surface
/// (`RasterGrid::Spacing`) when smaller or 0.
/// @throws std::invalid_argument if `albedo` has no source, holds a null layer, the source has a
/// band count other than 1 or 3, `grid` is empty, or the spacing is negative or not finite.
std::vector<Raster<float>> EvaluateAlbedo(const Albedo& albedo, const RasterGrid& grid,
                                          AlbedoContext context);

/// The albedo at world point (`x`, `y`): the cell centered there of `EvaluateAlbedo` over a grid
/// of `context.spacing`, to the same bits.
/// @throws std::invalid_argument as `EvaluateAlbedo`.
AlbedoSample EvaluateAlbedoAt(const Albedo& albedo, double x, double y,
                              const AlbedoContext& context);

/// Visits the albedo at one cell center: `visit(value, x, y)` may change `value`.
using AlbedoCellVisitor = std::function<void(AlbedoSample& value, double x, double y)>;

/// Calls `visit` for the albedo at every cell center, writing back what it leaves in
/// `value`: the cell loop shared by `AlbedoSource::Fill` and `AlbedoLayer::Apply`.
/// @param bands The bands, one raster per band on one grid.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
void ForEachAlbedoCell(std::vector<Raster<float>>& bands, unsigned threads,
                       const AlbedoCellVisitor& visit);

/// Visits the albedo at one cell by its column and row: `visit(value, col, row)` may change
/// `value`.
using AlbedoPixelVisitor =
    std::function<void(AlbedoSample& value, std::size_t col, std::size_t row)>;

/// `ForEachAlbedoCell` with each cell's column and row instead of its center, for layers that
/// prepare per-cell values over the whole grid.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
void ForEachAlbedoPixel(std::vector<Raster<float>>& bands, unsigned threads,
                        const AlbedoPixelVisitor& visit);

/// `ForEachAlbedoCell` in blocks of rows, for visitors that prepare per block, such as a
/// list of the features near it.
/// @param threads Threads to split the blocks over; 0 for every hardware thread.
/// @param blockRows Rows per block; positive.
/// @param makeVisitor `makeVisitor(rowBegin, rowEnd)` returns the visitor of the cells of rows
/// [`rowBegin`, `rowEnd`), called once per block on the thread that visits it.
void ForEachAlbedoBlock(
    std::vector<Raster<float>>& bands, unsigned threads, std::size_t blockRows,
    const std::function<AlbedoCellVisitor(std::size_t rowBegin, std::size_t rowEnd)>& makeVisitor);

}  // namespace terraingen
