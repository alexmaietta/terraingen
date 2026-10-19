/// @file
/// @brief Self-affine roughness of a base's heights by the variogram method, for layers that
/// continue it below the base's resolution.
#pragma once

#include "terraingen/noise/noise_layers.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <utility>

namespace terraingen {

/// Whether cell (`col`, `row`) of `heights` holds a finite value that is not no-data.
bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row);

/// The mean squared difference of cells `lag` apart along rows and columns where both hold data,
/// and how many pairs gave it.
std::pair<double, std::size_t> Variogram(const Raster<float>& heights, std::size_t lag);

/// A least-squares line through log2 RMS height difference over log2 lag in cells.
struct DeviogramFit {
    int lags = 0;            ///< Lags the line was fitted over; the slope needs at least 2.
    double slope = 0.0;      ///< The Hurst exponent H, unclamped.
    double intercept = 0.0;  ///< log2 of the RMS height difference at a lag of one cell.
};

/// The deviogram of `heights` (the square root of its `Variogram`) fitted at lags of 1, 2, 4 and
/// 8 cells, stopping at the first lag with no pairs or no relief.
DeviogramFit FitDeviogram(const Raster<float>& heights);

/// Self-affine roughness: the RMS height difference at a lag L is nu(L) = `deviation` (L /
/// `cellSize`)^H, so the RMS slope over a baseline b is nu(b) / b = s(c) (b / c)^(H - 1)
/// (Shepard et al. 2001; Rosenburg et al. 2011, eq. 4, https://doi.org/10.1029/2010JE003716).
struct BaseRoughness {
    double hurst = 1.0;      ///< Hurst exponent H, in (0, 1].
    double deviation = 0.0;  ///< RMS height difference at a lag of `cellSize`, in metres.
    double cellSize = 1.0;   ///< Lag `deviation` is given at, in metres.

    /// The RMS height difference in metres at a lag of `lag` metres.
    double Deviation(double lag) const;

    /// The RMS slope (rise over run) over a baseline of `baseline` metres.
    double RmsSlope(double baseline) const;
};

/// The roughness of `heights` on its own cells by the variogram method (`FitDeviogram`): H the
/// fitted slope, clamped to at most 1, and the deviation the fitted RMS height difference at one
/// cell. Cells with no data are skipped.
/// @param cellSize The side of a cell in metres, positive.
/// @throws std::invalid_argument for a `cellSize` that is not positive, for heights with relief at
/// fewer than two lags, or for a fitted Hurst exponent that is not positive.
BaseRoughness EstimateBaseRoughness(const Raster<float>& heights, double cellSize);

/// The heights of `source` on a lattice of `cellSize` cells with a node at `origin`, centred on
/// the source's grid, as many cells a side as its grid spans, from 3 up to 512.
/// @param cellSize Cell side in the source's units, positive.
Raster<float> SampleSourceWindow(const Source& source, double cellSize, const Vec2d& origin);

/// The roughness of `source` at its native resolution: `EstimateBaseRoughness` of
/// `SampleSourceWindow` on cells of the source's `ResolutionAt` its grid's centre, or of its
/// grid's cell size where that is infinite (a synthetic source), with a node half a cell in from
/// the grid's top-left corner: on the native cell centres of a raster resampled over its own
/// extent. Cell sizes in degrees convert to metres along a meridian (`Surface::UnitLength`).
/// @throws std::invalid_argument as `EstimateBaseRoughness`, as for a source without relief.
BaseRoughness EstimateSourceRoughness(const Source& source, const Surface& surface = {});

/// The amplitude in metres that gives `layer` the RMS height difference `roughness` has at a lag
/// of the layer's wavelength: `roughness.Deviation(wavelength)` over the layer's RMS height
/// difference at that lag with unit amplitude, measured on a fixed lattice of 64 x 64 points
/// 0.618 wavelengths apart in the plane.
/// @throws std::invalid_argument for a layer that shows no relief at unit amplitude.
float MatchedNoiseAmplitude(const NoiseLayer& layer, const BaseRoughness& roughness);

}  // namespace terraingen
