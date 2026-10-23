/// @file
/// @brief Self-affine roughness of a base's heights by the variogram method, for layers that
/// continue it below the base's resolution.
#pragma once

#include "terraingen/noise/noise_layers.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/variogram.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

namespace terraingen {

/// Self-affine roughness: the RMS height difference at a lag L is nu(L) = `deviation` (L /
/// `cellSize`)^H, so the RMS slope over a baseline b is nu(b) / b = s(c) (b / c)^(H - 1)
/// (Shepard et al. 2001; Rosenburg et al. 2011, eq. 4, https://doi.org/10.1029/2010JE003716).
struct BaseRoughness {
    double hurst = 1.0;      ///< Hurst exponent H, in (0, 1].
    double deviation = 0.0;  ///< RMS height difference at a lag of `cellSize`, in meters.
    double cellSize = 1.0;   ///< Lag `deviation` is given at, in meters.

    /// The RMS height difference in meters at a lag of `lag` meters.
    double Deviation(double lag) const;

    /// The RMS slope (rise over run) over a baseline of `baseline` meters.
    double RmsSlope(double baseline) const;
};

/// The heights of `source` on a lattice of `cellSize` cells with a node at `origin`, at a sample
/// spacing of `cellSize`, centered on the source's grid, as many cells a side as its grid spans,
/// from 3 up to 512; 512 a side centered on its reference point for a source without bounds.
/// @param cellSize Cell side in the source's units, positive.
Raster<float> SampleSourceWindow(const Source& source, double cellSize, const Vec2d& origin);

/// The roughness of `source` at its native resolution by the variogram method (`FitDeviogram`) of
/// `SampleSourceWindow` on its native cells: H the fitted slope, clamped to at most 1, and the
/// deviation the fitted RMS height difference at one cell; cells with no data are skipped.
///
/// The cells are the source's `ResolutionAt` its grid's center, or its grid's cell size where that
/// is infinite (a flat source), with a node half a cell in from the grid's top-left corner. A
/// source without bounds is sampled around its reference point on its posts. Cell sizes in
/// degrees convert to meters along a meridian (`Surface::UnitLength`).
/// @throws std::invalid_argument for heights with relief at fewer than two lags, a fitted Hurst
/// exponent that is not positive, or a source without bounds or a finite resolution.
BaseRoughness EstimateSourceRoughness(const Source& source, const Surface& surface = {});

/// The amplitude in meters that gives `layer` the RMS height difference `roughness` has at a lag
/// of the layer's wavelength: `roughness.Deviation(wavelength)` over the layer's RMS height
/// difference at that lag with unit amplitude, measured on a fixed lattice of 64 x 64 points
/// 0.618 wavelengths apart in the plane.
/// @throws std::invalid_argument for a layer that shows no relief at unit amplitude.
float MatchedNoiseAmplitude(const NoiseLayer& layer, const BaseRoughness& roughness);

}  // namespace terraingen
